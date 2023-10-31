/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2017 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2022-2023 RackTop Systems, Inc.
 */

/*
 * Helper functions for SMB signing using PKCS#11
 *
 * There are two implementations of these functions:
 * This one (for user space) and another for kernel.
 * See: uts/common/fs/smbsrv/smb_sign_kcf.c
 */

#include <stdlib.h>
#include <smbsrv/smb_kproto.h>
#include <smbsrv/smb_kcrypt.h>
#include <security/cryptoki.h>
#include <security/pkcs11.h>

/*
 * Common function to see if a mech is available.
 */
static int
find_mech(smb_crypto_mech_t *mech, ulong_t mid)
{
	CK_SESSION_HANDLE hdl;
	CK_RV rv;

	rv = SUNW_C_GetMechSession(mid, &hdl);
	if (rv != CKR_OK) {
		cmn_err(CE_NOTE, "PKCS#11: no mech 0x%x",
		    (unsigned int)mid);
		return (-1);
	}
	(void) C_CloseSession(hdl);

	mech->mechanism = mid;
	mech->pParameter = NULL;
	mech->ulParameterLen = 0;
	return (0);
}

/*
 * SMB1 signing helpers:
 * (getmech, init, update, final)
 */

/*
 * Find out if we have this mech.
 */
int
smb_md5_getmech(smb_crypto_mech_t *mech)
{
	return (find_mech(mech, CKM_MD5));
}

/*
 * Start PKCS#11 session.
 */
int
smb_md5_init(smb_sign_ctx_t *ctxp, smb_crypto_mech_t *mech)
{
	CK_RV rv;

	rv = SUNW_C_GetMechSession(mech->mechanism, ctxp);
	if (rv != CKR_OK)
		return (-1);

	rv = C_DigestInit(*ctxp, mech);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * Digest one segment
 */
int
smb_md5_update(smb_sign_ctx_t ctx, void *buf, size_t len)
{
	CK_RV rv;

	rv = C_DigestUpdate(ctx, buf, len);
	if (rv != CKR_OK)
		(void) C_CloseSession(ctx);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * Get the final digest.
 */
int
smb_md5_final(smb_sign_ctx_t ctx, uint8_t *digest16)
{
	CK_ULONG len = MD5_DIGEST_LENGTH;
	CK_RV rv;

	rv = C_DigestFinal(ctx, digest16, &len);
	(void) C_CloseSession(ctx);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * SMB2 signing helpers:
 * (getmech, init, update, final)
 */

/*
 * Find out if we have this mech.
 */
int
smb2_hmac_getmech(smb_crypto_mech_t *mech)
{
	return (find_mech(mech, CKM_SHA256_HMAC_GENERAL));
}

int
smb3_cmac_getmech(smb_crypto_mech_t *mech)
{
	return (find_mech(mech, CKM_AES_CMAC));
}

/*
 * The status of AES_GMAC in PKCS#11 is... complicated. We have GCM available,
 * and GMAC is just a subset of GCM, so just use that.
 */
int
smb3_gmac_getmech(smb_crypto_mech_t *mech)
{
	return (find_mech(mech, CKM_AES_GCM));
}

/*
 * Note, the SMB2 signature is the first 16 bytes of the digest,
 * even in the case of SHA256 HMAC (32-byte digest).
 *
 * CMAC has no parameter.
 */
void
smb2_sign_init_hmac_param(smb_crypto_mech_t *mech, smb_crypto_param_t *param,
    ulong_t hmac_len)
{
	param->hmac = hmac_len;

	mech->pParameter = (caddr_t)&param->hmac;
	mech->ulParameterLen = sizeof (param->hmac);
}

/*
 * GMAC is GCM where all data is specified as 'AAD', and IvLen is always 12.
 */
void
smb3_sign_init_gmac_param(smb_crypto_mech_t *mech, smb_crypto_param_t *param,
    uint8_t *iv)
{
	param->gcm.pIv = iv;
	param->gcm.ulIvLen = SMB3_AES_GMAC_NONCE_SIZE;
	param->gcm.ulTagBits = SMB2_SIG_SIZE << 3;	/* bytes to bits */
	/* We'll update these in smb2_mac_uio() */
	param->gcm.pAAD = NULL;
	param->gcm.ulAADLen = 0;

	mech->pParameter = (caddr_t)&param->gcm;
	mech->ulParameterLen = sizeof (param->gcm);
}

int
smb2_mac_raw(smb_crypto_mech_t *mech,
    uint8_t *key, size_t key_len,
    uint8_t *data, size_t data_len,
    uint8_t *mac, size_t mac_len)
{
	CK_SESSION_HANDLE hssn = 0;
	CK_OBJECT_HANDLE hkey = 0;
	CK_ULONG ck_maclen = mac_len;
	CK_RV rv;
	int rc = 0;

	rv = SUNW_C_GetMechSession(mech->mechanism, &hssn);
	if (rv != CKR_OK)
		return (-1);

	rv = SUNW_C_KeyToObject(hssn, mech->mechanism,
	    key, key_len, &hkey);
	if (rv != CKR_OK) {
		rc = -2;
		goto out;
	}

	/*
	 * Now that we know the input length, update the parameter for GMAC.
	 */
	if (mech->mechanism == CKM_AES_GCM) {
		CK_GCM_PARAMS *param = mech->pParameter;

		param->pAAD = data;
		param->ulAADLen = data_len;

		C_EncryptInit(hssn, mech, hkey);
		if (rv != CKR_OK) {
			rc = -3;
			goto out;
		}
		rv = C_Encrypt(hssn, NULL, 0, mac, &ck_maclen);
	} else {
		rv = C_SignInit(hssn, mech, hkey);
		if (rv != CKR_OK) {
			rc = -3;
			goto out;
		}

		rv = C_Sign(hssn, data, data_len, mac, &ck_maclen);
	}

	if (rv != CKR_OK)
		rc = -4;
	else if (ck_maclen != mac_len)
		rc = -5;
	else
		rc = 0;

out:
	if (hkey != 0)
		(void) C_DestroyObject(hssn, hkey);
	if (hssn != 0)
		(void) C_CloseSession(hssn);

	return (rc);
}

/*
 * While the PKCS#11 implementation internally has the ability to
 * handle scatter/gather, it currently presents no interface for it.
 * As this library is used primarily for debugging, performance in
 * here is not a big concern, so we'll get around the limitation of
 * libpkcs11 by copying to/from a contiguous working buffer.
 */
static int
smb2_gmac_flatten(smb_crypto_mech_t *mech, uint8_t *key, size_t key_len,
    uio_t *in_uio, uint8_t *digest16)
{
	uint8_t *buf = NULL;
	size_t inlen;
	int err, rc = -1;

	inlen = in_uio->uio_resid;
	buf = malloc(inlen);
	if (buf == NULL)
		return (rc);

	/* Copy from uio segs to buf */
	err = uiomove(buf, inlen, UIO_WRITE, in_uio);
	if (err == 0)
		rc = smb2_mac_raw(mech, key, key_len, buf, inlen,
		    digest16, SMB2_SIG_SIZE);

	free(buf);

	return (rc);
}

/*
 * Digest a whole message with scatter/gather (UIO)
 */
int
smb2_mac_uio(smb_crypto_mech_t *mech, uint8_t *key, size_t key_len,
    uio_t *in_uio, uint8_t *digest16)
{
	CK_SESSION_HANDLE hssn = 0;
	CK_OBJECT_HANDLE hkey = 0;
	CK_ULONG mac_len = SMB2_SIG_SIZE;
	CK_ULONG ck_maclen = mac_len;
	CK_RV rv;
	int rc = 0;

	if (in_uio->uio_resid <= 0)
		return (-1);

	/* We map GMAC to GCM, which requires a single flat buffer. */
	if (mech->mechanism == CKM_AES_GCM) {
		rc = smb2_gmac_flatten(mech, key, key_len, in_uio, digest16);
		return (rc);
	}

	if (in_uio->uio_segflg != UIO_USERSPACE &&
	    in_uio->uio_segflg != UIO_SYSSPACE) {
		rc = EINVAL;
		goto out;
	}

	rv = SUNW_C_GetMechSession(mech->mechanism, &hssn);
	if (rv != CKR_OK)
		return (-1);

	rv = SUNW_C_KeyToObject(hssn, mech->mechanism,
	    key, key_len, &hkey);
	if (rv != CKR_OK) {
		rc = -2;
		goto out;
	}

	rv = C_SignInit(hssn, mech, hkey);
	if (rv != CKR_OK) {
		rc = -3;
		goto out;
	}

	/* Like uiomove() */
	while (in_uio->uio_resid > 0) {
		struct iovec *iov = in_uio->uio_iov;
		CK_ULONG data_len = MIN(in_uio->uio_resid, iov->iov_len);

		if (data_len == 0) {
			in_uio->uio_iov++;
			in_uio->uio_iovcnt--;
			continue;
		}

		rv = C_SignUpdate(hssn, (CK_BYTE_PTR)iov->iov_base, data_len);
		if (rv != CKR_OK) {
			rc = -4;
			goto out;
		}

		iov->iov_base += data_len;
		iov->iov_len -= data_len;
		in_uio->uio_resid -= data_len;
		in_uio->uio_loffset += data_len;
	}
	rv = C_SignFinal(hssn, digest16, &ck_maclen);

	if (rv != CKR_OK)
		rc = -5;
	else if (ck_maclen != mac_len)
		rc = -6;
	else
		rc = 0;

out:
	if (hkey != 0)
		(void) C_DestroyObject(hssn, hkey);
	if (hssn != 0)
		(void) C_CloseSession(hssn);

	return (rc);
}
