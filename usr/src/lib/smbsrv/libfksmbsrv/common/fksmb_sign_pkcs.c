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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * Helper functions for SMB2/3 signing using PKCS#11
 *
 * There are two implementations of these functions:
 * This one (for user space) and another for kernel.
 * See: uts/common/fs/smbsrv/smb2_sign_kcf.c
 */

#include <stdlib.h>
#include <smbsrv/smb2_kproto.h>
#include <smbsrv/smb2_signing.h>
#include <security/cryptoki.h>
#include <security/pkcs11.h>

/*
 * SMB2 signing helpers:
 * (getmech, init, update, final)
 */

int
smb2_hmac_getmech(smb2_sign_mech_t *mech)
{
	mech->mechanism = CKM_SHA256_HMAC;
	mech->pParameter = NULL;
	mech->ulParameterLen = 0;
	return (0);
}

/*
 * Start PKCS#11 session, load the key.
 */
int
smb2_hmac_init(smb2_sign_ctx_t *ctxp, smb2_sign_mech_t *mech,
    uint8_t *key, size_t key_len)
{
	CK_OBJECT_HANDLE hkey = 0;
	CK_RV rv;

	rv = SUNW_C_GetMechSession(mech->mechanism, ctxp);
	if (rv != CKR_OK)
		return (-1);

	rv = SUNW_C_KeyToObject(*ctxp, mech->mechanism,
	    key, key_len, &hkey);
	if (rv != CKR_OK)
		return (-1);

	rv = C_SignInit(*ctxp, mech, hkey);
	(void) C_DestroyObject(*ctxp, hkey);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * Digest one segment
 */
int
smb2_hmac_update(smb2_sign_ctx_t ctx, uint8_t *in, size_t len)
{
	CK_RV rv;

	rv = C_SignUpdate(ctx, in, len);
	if (rv != CKR_OK)
		(void) C_CloseSession(ctx);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * Note, the SMB2 signature is the first 16 bytes of the
 * 32-byte SHA256 HMAC digest.
 */
int
smb2_hmac_final(smb2_sign_ctx_t ctx, uint8_t *digest16)
{
	uint8_t full_digest[SHA256_DIGEST_LENGTH];
	CK_ULONG len = SHA256_DIGEST_LENGTH;
	CK_RV rv;

	rv = C_SignFinal(ctx, full_digest, &len);
	if (rv == CKR_OK)
		bcopy(full_digest, digest16, 16);

	(void) C_CloseSession(ctx);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * SMB3 signing helpers:
 * (getmech, init, update, final)
 */

int
smb3_cmac_getmech(smb2_sign_mech_t *mech)
{
	mech->mechanism = CKM_AES_CMAC;
	mech->pParameter = NULL;
	mech->ulParameterLen = 0;
	return (0);
}

/*
 * Start PKCS#11 session, load the key.
 */
int
smb3_cmac_init(smb2_sign_ctx_t *ctxp, smb2_sign_mech_t *mech,
    uint8_t *key, size_t key_len)
{
	CK_OBJECT_HANDLE hkey = 0;
	CK_RV rv;

	rv = SUNW_C_GetMechSession(mech->mechanism, ctxp);
	if (rv != CKR_OK)
		return (-1);

	rv = SUNW_C_KeyToObject(*ctxp, mech->mechanism,
	    key, key_len, &hkey);
	if (rv != CKR_OK)
		return (-1);

	rv = C_SignInit(*ctxp, mech, hkey);
	(void) C_DestroyObject(*ctxp, hkey);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * Digest one segment
 */
int
smb3_cmac_update(smb2_sign_ctx_t ctx, uint8_t *in, size_t len)
{
	CK_RV rv;

	rv = C_SignUpdate(ctx, in, len);
	if (rv != CKR_OK)
		(void) C_CloseSession(ctx);

	return (rv == CKR_OK ? 0 : -1);
}

/*
 * Note, the SMB2 signature is just the AES CMAC digest.
 * (both are 16 bytes long)
 */
int
smb3_cmac_final(smb2_sign_ctx_t ctx, uint8_t *digest)
{
	CK_ULONG len = SMB2_SIG_SIZE;
	CK_RV rv;

	rv = C_SignFinal(ctx, digest, &len);
	(void) C_CloseSession(ctx);

	return (rv == CKR_OK ? 0 : -1);
}
