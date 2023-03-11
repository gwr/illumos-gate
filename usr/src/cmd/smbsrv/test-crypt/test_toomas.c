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
 * Copyright 2021 Tintri by DDN, Inc. All rights reserved.
 * Copyright 2023 RackTop Systems, Inc.
 */

#include <sys/types.h>
#include <smbsrv/smb_kcrypt.h>
#include <security/cryptoki.h>
#include <security/pkcs11.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// #include "test_data.h"
#include "utils.h"

/*
 * Don't use static data for a "nonce" in real life!
 */
char authdata[] = "Authentication16";
char keydata[]  = "The Key  Data 16";
char nonce[]    = "Noncedata11";

#define	CLEAR_DATA_LEN		110
#define	CIPHER_DATA_LEN		(CLEAR_DATA_LEN + 16)

const char clear_data_ref[CLEAR_DATA_LEN] = {
	'\xfe', '\x53', '\x4d', '\x42',
	'\x40', '\x00', '\x01', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x10', '\x00', '\x01', '\x00', '\x01', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x2e', '\x5a', '\x26', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\xff', '\xfe', '\x00', '\x00',
	'\x01', '\x00', '\x00', '\x00', '\x77', '\x23', '\xd5', '\xc3',
	'\xd2', '\x01', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x09', '\x00', '\x48', '\x00',
	'\x26', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x0e', '\x00', '\x00', '\x00', '\x00', '\x10', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x00', '\x10', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x3a', '\x00', '\x3a', '\x00',
	'\x24', '\x00', '\x44', '\x00', '\x41', '\x00', '\x54', '\x00',
	'\x41', '\x00'
};

// Cipher for the above, using CKM_AES_CCM

const uint8_t cipher_data_ccm[CIPHER_DATA_LEN] = {
	'\xbc', '\xcb', '\x91', '\x73',  '\x52', '\xf6', '\x2b', '\x1e',
	'\xf4', '\x6d', '\x69', '\x3b',  '\xf9', '\x2f', '\x53', '\xf3',
	'\x95', '\x0a', '\xfe', '\xc1',  '\x7c', '\xad', '\xa4', '\xcd',
	'\x2e', '\x64', '\x2e', '\x3a',  '\x7f', '\xf0', '\xc6', '\x0b',
	'\xb6', '\x3f', '\x21', '\x90',  '\xd7', '\x2a', '\x89', '\xc1',
	'\xcd', '\x27', '\x60', '\xd0',  '\x0d', '\x6d', '\xc4', '\xf8',
	'\x38', '\x5b', '\xe3', '\x17',  '\xe8', '\x1a', '\xb1', '\x66',
	'\x9b', '\x84', '\xe4', '\x2a',  '\xdd', '\xe1', '\xee', '\x3c',
	'\x21', '\x09', '\x8c', '\x0d',  '\xd1', '\x9e', '\x12', '\x11',
	'\xeb', '\x5b', '\x86', '\x86',  '\x02', '\x26', '\x91', '\x31',
	'\xa2', '\xfc', '\x1c', '\xef',  '\x3f', '\x1e', '\x21', '\xd9',
	'\x12', '\x4e', '\x99', '\x31',  '\x2e', '\x0f', '\xea', '\x9f',
	'\xc8', '\x47', '\x22', '\xa3',  '\xfe', '\x27', '\xdf', '\x84',
	'\x6e', '\x9b', '\xd5', '\xdb',  '\x12', '\xa7', '\xb6', '\xab',
	'\x37', '\xa8', '\xb6', '\xc8',  '\xa3', '\x44', '\x6c', '\xe3',
	'\x86', '\xb5', '\x3d', '\x4c',  '\x3a', '\x1f' };

// Cipher for the above, using CKM_AES_GCM

const uint8_t cipher_data_gcm[CIPHER_DATA_LEN] = {
	'\x57', '\xf3', '\x21', '\x8d',  '\xb4', '\xcc', '\x0f', '\xa0',
	'\xd3', '\x7a', '\x26', '\x2d',  '\x3d', '\x33', '\x98', '\x98',
	'\xe1', '\x69', '\x5f', '\x6e',  '\x33', '\x9f', '\xc6', '\xe9',
	'\x01', '\xaa', '\xa4', '\x5b',  '\xfe', '\x6e', '\xdb', '\x6a',
	'\x78', '\xdd', '\x34', '\x43',  '\x0a', '\x26', '\x0b', '\xd6',
	'\x1f', '\x11', '\x72', '\xea',  '\xea', '\x59', '\xe7', '\x08',
	'\x04', '\x18', '\xcf', '\x11',  '\xe9', '\x9d', '\x56', '\xc0',
	'\xa7', '\xc9', '\xb8', '\x3a',  '\xaf', '\xd3', '\x22', '\x7c',
	'\x91', '\xab', '\xb9', '\xcf',  '\x24', '\x97', '\x94', '\x9c',
	'\x20', '\x6f', '\x4d', '\x64',  '\x74', '\x02', '\x93', '\xe6',
	'\xa5', '\xc2', '\xa5', '\x2c',  '\x28', '\x42', '\x8f', '\x26',
	'\x1a', '\xc3', '\xf9', '\x46',  '\x2a', '\x46', '\x84', '\x8d',
	'\x47', '\xca', '\xc6', '\xd1',  '\x04', '\x2d', '\x40', '\x6c',
	'\x71', '\xd0', '\x7b', '\x84',  '\xe6', '\x24', '\xbb', '\x7a',
	'\xf7', '\x5f', '\xd3', '\x7f',  '\x69', '\x1a', '\x83', '\x2b',
	'\x9f', '\x0f', '\x0b', '\x2e',  '\x26', '\xa0' };

uint8_t outbuf[CIPHER_DATA_LEN];

/*
 * Test program for the interfaces used in
 * smb3_encrypt_reply()
 */
int
do_encrypt(uint8_t *outbuf, size_t *outlen,
    const char *inbuf, size_t inlen, int mid)
{
	smb_enc_ctx_t ctx;
	uio_t uio_in;
	uio_t uio_out;
	iovec_t iov_in[4];
	iovec_t iov_out[4];
	int rc;

	bzero(&ctx, sizeof (ctx));
	ctx.mech.mechanism = mid; // CKM_AES_CCM or CKM_AES_GCM

	switch (mid) {

	case CKM_AES_CCM:
		smb3_crypto_init_ccm_param(&ctx,
		    (uint8_t *)nonce, 11,
		    (uint8_t *)authdata, 16,
		    inlen);
		break;

	case CKM_AES_GCM:
		smb3_crypto_init_gcm_param(&ctx,
		    (uint8_t *)nonce, 12,
		    (uint8_t *)authdata, 16);
		break;

	default:
		return (1);
	}

	rc = smb3_encrypt_init(&ctx,
	    (uint8_t *)keydata, 16);
	if (rc != 0)
		return (rc);

	make_uio((void *)inbuf, inlen, &uio_in, iov_in, 4);
	make_uio(outbuf, *outlen, &uio_out, iov_out, 4);
	*outlen = uio_out.uio_resid;

	rc = smb3_encrypt_uio(&ctx, &uio_in, &uio_out);
	*outlen -= uio_out.uio_resid;

	smb3_enc_ctx_done(&ctx);

	return (rc);
}

void
test_encrypt(const uint8_t *ref, int mid)
{
	size_t outlen;
	int rc;

	outlen = sizeof (outbuf);
	rc = do_encrypt(outbuf, &outlen,
	    clear_data_ref, CLEAR_DATA_LEN, mid);
	if (rc != 0) {
		printf("FAIL: encrypt rc= %d\n");
		return;
	}

	if (outlen != CIPHER_DATA_LEN) {
		printf("FAIL: out len = %d (want %d)\n",
		    outlen, CIPHER_DATA_LEN);
		return;
	}

	if (memcmp(outbuf, ref, CIPHER_DATA_LEN) != 0) {
		printf("FAIL: ciphertext:\n");
		hexdump(outbuf, CIPHER_DATA_LEN);
		return;
	}

	printf("PASS mid=0x%x\n", mid);
}

int
main(int argc, char *argv[])
{

	test_encrypt(cipher_data_ccm, CKM_AES_CCM);
	test_encrypt(cipher_data_gcm, CKM_AES_GCM);

	return (0);
}
