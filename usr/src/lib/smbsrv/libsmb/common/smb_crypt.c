/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2012 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2026 RackTop Systems, Inc.
 */

#include <sys/md4.h>
#include <sys/sha2.h>
#include <sys/types.h>
#include <string.h>
#include <security/cryptoki.h>
#include <security/pkcs11.h>
#include <cryptoutil.h>
#include <smbsrv/libsmb.h>

static void smb_initlmkey(unsigned char *keyin, unsigned char *keyout);

/*
 * randomize
 *
 * Randomize the contents of the specified buffer.
 */
void
randomize(char *data, unsigned len)
{
	char *p = data;

	if (pkcs11_get_random(data, len) == 0)
		return;

	/*
	 * Implement a "fall back", because current callers
	 * don't expect an error from this.  In practice,
	 * we never use this fall back.
	 */
	while (len--) {
		*p++ = (random() >> 24);
	}
}

/*
 * smb_auth_md4
 *
 * Compute an MD4 digest.
 */
int
smb_auth_md4(unsigned char *result, unsigned char *input, int length)
{
	MD4_CTX md4_context;

	MD4Init(&md4_context);
	MD4Update(&md4_context, input, length);
	MD4Final(result, &md4_context);
	return (SMBAUTH_SUCCESS);
}

/*
 * smb_auth_hmac_md5
 *
 * Compute HMAC_MD5 over a single buffer.
 */
int
smb_auth_hmac_md5(unsigned char *data,
    int data_len,
    unsigned char *key,
    int key_len,
    unsigned char *digest)
{
	CK_RV rv;
	CK_MECHANISM mechanism;
	CK_OBJECT_HANDLE hKey;
	CK_SESSION_HANDLE hSession;
	CK_ULONG diglen = MD_DIGEST_LEN;

	mechanism.mechanism = CKM_MD5_HMAC;
	mechanism.pParameter = 0;
	mechanism.ulParameterLen = 0;
	rv = SUNW_C_GetMechSession(mechanism.mechanism, &hSession);
	if (rv != CKR_OK) {
		return (SMBAUTH_FAILURE);
	}

	rv = SUNW_C_KeyToObject(hSession, mechanism.mechanism,
	    key, key_len, &hKey);
	if (rv != CKR_OK) {
		(void) C_CloseSession(hSession);
		return (SMBAUTH_FAILURE);
	}

	/* Initialize the digest operation in the session */
	rv = C_SignInit(hSession, &mechanism, hKey);
	if (rv != CKR_OK) {
		(void) C_DestroyObject(hSession, hKey);
		(void) C_CloseSession(hSession);
		return (SMBAUTH_FAILURE);
	}
	rv = C_SignUpdate(hSession, (CK_BYTE_PTR)data, data_len);
	if (rv != CKR_OK) {
		(void) C_DestroyObject(hSession, hKey);
		(void) C_CloseSession(hSession);
		return (SMBAUTH_FAILURE);
	}
	rv = C_SignFinal(hSession, (CK_BYTE_PTR)digest, &diglen);
	if (rv != CKR_OK) {
		(void) C_DestroyObject(hSession, hKey);
		(void) C_CloseSession(hSession);
		return (SMBAUTH_FAILURE);
	}
	(void) C_DestroyObject(hSession, hKey);
	(void) C_CloseSession(hSession);
	if (diglen != MD_DIGEST_LEN) {
		return (SMBAUTH_FAILURE);
	}
	return (SMBAUTH_SUCCESS);
}

/*
 * smb_auth_hmac_sha256
 *
 * Compute HMAC_SHA256 over one or more buffers defined by iov.
 */
int
smb_auth_hmac_sha256(const iovec_t *iov, size_t niov,
    const unsigned char *key, size_t key_len, unsigned char *digest)
{
	CK_MECHANISM mechanism = { CKM_SHA256_HMAC, NULL, 0 };
	CK_SESSION_HANDLE hsession;
	CK_OBJECT_HANDLE hkey = 0;
	CK_ULONG diglen = SHA256_DIGEST_LENGTH;
	CK_RV rv;
	size_t i;

	if (key == NULL || key_len == 0 || digest == NULL ||
	    (niov != 0 && iov == NULL))
		return (SMBAUTH_FAILURE);

	rv = SUNW_C_GetMechSession(mechanism.mechanism, &hsession);
	if (rv != CKR_OK)
		return (SMBAUTH_FAILURE);

	rv = SUNW_C_KeyToObject(hsession, mechanism.mechanism,
	    key, key_len, &hkey);
	if (rv == CKR_OK)
		rv = C_SignInit(hsession, &mechanism, hkey);
	for (i = 0; i < niov && rv == CKR_OK; i++) {
		if (iov[i].iov_len == 0)
			continue;
		if (iov[i].iov_base == NULL) {
			rv = CKR_ARGUMENTS_BAD;
			break;
		}
		rv = C_SignUpdate(hsession, (CK_BYTE_PTR)iov[i].iov_base,
		    iov[i].iov_len);
	}
	if (rv == CKR_OK)
		rv = C_SignFinal(hsession, digest, &diglen);

	if (hkey != 0)
		(void) C_DestroyObject(hsession, hkey);
	(void) C_CloseSession(hsession);

	if (rv != CKR_OK || diglen != SHA256_DIGEST_LENGTH)
		return (SMBAUTH_FAILURE);
	return (SMBAUTH_SUCCESS);
}

int
smb_auth_DES(unsigned char *Result, int ResultLen,
    unsigned char *Key, int KeyLen,
    unsigned char *Data, int DataLen)
{
	CK_RV rv;
	CK_MECHANISM mechanism;
	CK_OBJECT_HANDLE hKey;
	CK_SESSION_HANDLE hSession;
	CK_ULONG ciphertext_len;
	uchar_t des_key[8];
	int error = 0;
	int K, D;
	int k, d;

	/*
	 * Calculate proper number of iterations.
	 * Known call cases include:
	 *   ResultLen=16, KeyLen=14, DataLen=8
	 *   ResultLen=24, KeyLen=21, DataLen=8
	 *   ResultLen=16, KeyLen=14, DataLen=16
	 */
	K = KeyLen / 7;
	D = DataLen / 8;
	if ((KeyLen % 7) || (DataLen % 8))
		return (EINVAL);
	if (K == 0 || D == 0)
		return (EINVAL);
	if (ResultLen < (K * 8))
		return (EINVAL);

	/*
	 * Use SUNW convenience function to initialize the cryptoki
	 * library, and open a session with a slot that supports
	 * the mechanism we plan on using.
	 */
	mechanism.mechanism = CKM_DES_ECB;
	mechanism.pParameter = NULL;
	mechanism.ulParameterLen = 0;
	rv = SUNW_C_GetMechSession(mechanism.mechanism, &hSession);
	if (rv != CKR_OK) {
		return (SMBAUTH_FAILURE);
	}

	for (d = k = 0; k < K; k++, d++) {
		/* Cycle the input again, as necessary. */
		if (d == D)
			d = 0;
		smb_initlmkey(&Key[k * 7], des_key);
		rv = SUNW_C_KeyToObject(hSession, mechanism.mechanism,
		    des_key, 8, &hKey);
		if (rv != CKR_OK) {
			error = 1;
			goto exit_session;
		}
		/* Initialize the encryption operation in the session */
		rv = C_EncryptInit(hSession, &mechanism, hKey);
		if (rv != CKR_OK) {
			error = 1;
			goto exit_encrypt;
		}
		ciphertext_len = 8;

		/* Read in the data and encrypt this portion */
		rv = C_EncryptUpdate(hSession,
		    (CK_BYTE_PTR)Data + (d * 8), 8,
		    (CK_BYTE_PTR)Result + (k * 8),
		    &ciphertext_len);
		if (rv != CKR_OK) {
			error = 1;
			goto exit_encrypt;
		}

		(void) C_DestroyObject(hSession, hKey);
	}
	goto exit_session;

exit_encrypt:
	(void) C_DestroyObject(hSession, hKey);
exit_session:
	(void) C_CloseSession(hSession);

	if (error)
		return (SMBAUTH_FAILURE);

	return (SMBAUTH_SUCCESS);
}

/*
 * See "Netlogon Credential Computation" section of MS-NRPC document.
 */
static void
smb_initlmkey(unsigned char *keyin, unsigned char *keyout)
{
	int i;

	keyout[0] = keyin[0] >> 0x01;
	keyout[1] = ((keyin[0] & 0x01) << 6) | (keyin[1] >> 2);
	keyout[2] = ((keyin[1] & 0x03) << 5) | (keyin[2] >> 3);
	keyout[3] = ((keyin[2] & 0x07) << 4) | (keyin[3] >> 4);
	keyout[4] = ((keyin[3] & 0x0f) << 3) | (keyin[4] >> 5);
	keyout[5] = ((keyin[4] & 0x1f) << 2) | (keyin[5] >> 6);
	keyout[6] = ((keyin[5] & 0x3f) << 1) | (keyin[6] >> 7);
	keyout[7] = keyin[6] & 0x7f;

	for (i = 0; i < 8; i++)
		keyout[i] = (keyout[i] << 1) & 0xfe;
}

/*
 * CKM_RC4
 */
int
smb_auth_RC4(uchar_t *Result, int ResultLen,
    uchar_t *Key, int KeyLen,
    uchar_t *Data, int DataLen)
{
	CK_RV rv;
	CK_MECHANISM mechanism;
	CK_OBJECT_HANDLE hKey;
	CK_SESSION_HANDLE hSession;
	CK_ULONG ciphertext_len;
	int error = SMBAUTH_FAILURE;

	/*
	 * Use SUNW convenience function to initialize the cryptoki
	 * library, and open a session with a slot that supports
	 * the mechanism we plan on using.
	 */
	mechanism.mechanism = CKM_RC4;
	mechanism.pParameter = NULL;
	mechanism.ulParameterLen = 0;
	rv = SUNW_C_GetMechSession(mechanism.mechanism, &hSession);
	if (rv != CKR_OK) {
		return (SMBAUTH_FAILURE);
	}

	rv = SUNW_C_KeyToObject(hSession, mechanism.mechanism,
	    Key, KeyLen, &hKey);
	if (rv != CKR_OK)
		goto exit_session;

	/* Initialize the encryption operation in the session */
	rv = C_EncryptInit(hSession, &mechanism, hKey);
	if (rv != CKR_OK)
		goto exit_encrypt;

	ciphertext_len = ResultLen;
	rv = C_EncryptUpdate(hSession,
	    (CK_BYTE_PTR)Data, DataLen,
	    (CK_BYTE_PTR)Result, &ciphertext_len);
	if (rv == CKR_OK)
		error = 0;

exit_encrypt:
	(void) C_DestroyObject(hSession, hKey);
exit_session:
	(void) C_CloseSession(hSession);

	return (error);
}

/*
 * Encrypt or decrypt one or more buffers with one AES-CFB8 operation.
 */
int
smb_auth_AES_CFB8(const iovec_t *input, const iovec_t *output, size_t count,
    uchar_t *key, size_t key_len, uchar_t *iv, boolean_t decrypt)
{
	CK_RV rv;
	CK_MECHANISM mechanism;
	CK_OBJECT_HANDLE hkey;
	CK_SESSION_HANDLE hsession;
	CK_ULONG output_len;
	uchar_t final[16];
	size_t i;
	boolean_t has_data = B_FALSE;
	int error = SMBAUTH_FAILURE;

	if (input == NULL || output == NULL || count == 0 ||
	    key == NULL || key_len != 16 || iv == NULL)
		return (SMBAUTH_FAILURE);

	for (i = 0; i < count; i++) {
		if (input[i].iov_len != output[i].iov_len)
			return (SMBAUTH_FAILURE);
		if (input[i].iov_len == 0)
			continue;
		if (input[i].iov_base == NULL || output[i].iov_base == NULL)
			return (SMBAUTH_FAILURE);
		has_data = B_TRUE;
	}
	if (!has_data)
		return (SMBAUTH_FAILURE);

	mechanism.mechanism = CKM_AES_CFB8;
	mechanism.pParameter = iv;
	mechanism.ulParameterLen = 16;
	rv = SUNW_C_GetMechSession(mechanism.mechanism, &hsession);
	if (rv != CKR_OK)
		return (SMBAUTH_FAILURE);

	rv = SUNW_C_KeyToObject(hsession, mechanism.mechanism, key, key_len,
	    &hkey);
	if (rv != CKR_OK)
		goto exit_session;

	if (decrypt)
		rv = C_DecryptInit(hsession, &mechanism, hkey);
	else
		rv = C_EncryptInit(hsession, &mechanism, hkey);
	if (rv != CKR_OK)
		goto exit_key;

	for (i = 0; i < count; i++) {
		if (input[i].iov_len == 0)
			continue;

		output_len = output[i].iov_len;
		if (decrypt) {
			rv = C_DecryptUpdate(hsession,
			    (CK_BYTE_PTR)input[i].iov_base, input[i].iov_len,
			    (CK_BYTE_PTR)output[i].iov_base, &output_len);
		} else {
			rv = C_EncryptUpdate(hsession,
			    (CK_BYTE_PTR)input[i].iov_base, input[i].iov_len,
			    (CK_BYTE_PTR)output[i].iov_base, &output_len);
		}
		if (rv != CKR_OK || output_len != output[i].iov_len)
			goto exit_key;
	}

	output_len = sizeof (final);
	if (decrypt)
		rv = C_DecryptFinal(hsession, final, &output_len);
	else
		rv = C_EncryptFinal(hsession, final, &output_len);
	if (rv == CKR_OK && output_len == 0)
		error = SMBAUTH_SUCCESS;

exit_key:
	explicit_bzero(final, sizeof (final));
	(void) C_DestroyObject(hsession, hkey);
exit_session:
	(void) C_CloseSession(hsession);
	return (error);
}
