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
 * Copyright 2026 RackTop Systems, Inc.
 */

/*
 * Scatter-gather test backend.  This calls the AES mode code in
 * libsoftcrypto directly (much as the aes kernel module does) and has it
 * write its output into a CRYPTO_DATA_UIO or CRYPTO_DATA_MBLK buffer, as
 * selected by cryptotest_sg_format (see cryptotest_uio.c and
 * cryptotest_mblk.c).  KCF consumers can pass such buffers, but the
 * /dev/crypto ioctls only use CRYPTO_DATA_RAW, so this is the only way
 * these tests can exercise the mode code's handling of segmented output.
 *
 * Every output buffer is split into many separately allocated segments of
 * varied (including zero) lengths so that cipher blocks straddle several
 * segments, and the output starts part way into the chain.  Each segment
 * is bracketed by guard bytes, and the bytes outside the expected output
 * range must be left untouched; this catches writes past the end of a
 * segment as well as misplaced output.
 */

/*
 * The test Makefiles build with _FILE_OFFSET_BITS=64, but libsoftcrypto
 * does not, and crypto_data_t contains an off_t.  Use the library's view
 * of crypto_data_t so 32-bit programs agree with the library.
 */
#undef	_FILE_OFFSET_BITS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/stream.h>
#include <security/cryptoki.h>
#include <modes/modes.h>
#include <aes/aes_impl.h>

#include "cryptotest.h"

boolean_t cryptotest_pkcs = B_FALSE;	/* true if PKCS */

struct crypto_op {
	uint8_t *in;
	uint8_t *out;
	uint8_t *key;
	char *param;

	size_t inlen;
	size_t outlen;
	size_t keylen;		/* bytes */
	size_t paramlen;

	char *mechname;
	crypto_func_group_t fg;

	/* internal */
	uint32_t mode;
	aes_ctx_t *ctx;
};

static const struct {
	const char *sm_name;
	uint32_t sm_mode;
} sg_mechs[] = {
	{ SUN_CKM_AES_ECB, ECB_MODE },
	{ SUN_CKM_AES_CBC, CBC_MODE },
	{ SUN_CKM_AES_CFB8, CFB_MODE },
	{ SUN_CKM_AES_CTR, CTR_MODE },
	{ SUN_CKM_AES_CCM, CCM_MODE },
	{ SUN_CKM_AES_GCM, GCM_MODE }
};

/*
 * Output buffer layout.  The output starts SG_PREFIX bytes into the chain.
 * Segment lengths are taken from sg_pattern, starting at a different place
 * for each buffer.  A zero-length segment is always added at the end.
 */
#define	SG_PREFIX	5
#define	SG_GUARD_LEN	8
#define	SG_GUARD_BYTE	0xa5
#define	SG_FILL_BYTE	0x5a

static const size_t sg_pattern[] = {
	0, 1, 3, 2, 7, 0, 5, 1, 17, 4, 11, 1, 1, 16, 9, 64, 33
};
#define	SG_NPATTERN	(sizeof (sg_pattern) / sizeof (sg_pattern[0]))

static uint_t sg_start;

typedef struct sg_buf {
	crypto_data_t	sb_cd;
	size_t		sb_space;	/* output space after SG_PREFIX */
	uint_t		sb_nsegs;
	uint8_t		**sb_bufs;	/* guard, data, guard per segment */
	size_t		*sb_lens;	/* data length per segment */
	iovec_t		*sb_iov;
	uio_t		sb_uio;
	mblk_t		*sb_mblks;
} sg_buf_t;

#define	SG_DATA(sb, i)	((sb)->sb_bufs[i] + SG_GUARD_LEN)

static void
sg_free(sg_buf_t *sb)
{
	uint_t i;

	if (sb->sb_bufs != NULL) {
		for (i = 0; i < sb->sb_nsegs; i++)
			free(sb->sb_bufs[i]);
	}
	free(sb->sb_bufs);
	free(sb->sb_lens);
	free(sb->sb_iov);
	free(sb->sb_mblks);
	bzero(sb, sizeof (*sb));
}

static int
sg_alloc(sg_buf_t *sb, size_t space)
{
	size_t total = SG_PREFIX + space;
	size_t sum, len;
	uint_t i, n, start;

	bzero(sb, sizeof (*sb));
	sb->sb_space = space;

	start = sg_start++ % SG_NPATTERN;
	for (n = 0, sum = 0; sum < total; n++)
		sum += sg_pattern[(start + n) % SG_NPATTERN];
	n++;

	sb->sb_nsegs = n;
	sb->sb_bufs = calloc(n, sizeof (uint8_t *));
	sb->sb_lens = calloc(n, sizeof (size_t));
	if (sb->sb_bufs == NULL || sb->sb_lens == NULL)
		goto nomem;

	for (i = 0, sum = 0; i < n; i++) {
		len = 0;
		if (i < n - 1) {
			len = MIN(sg_pattern[(start + i) % SG_NPATTERN],
			    total - sum);
		}
		sb->sb_lens[i] = len;
		sb->sb_bufs[i] = malloc(len + 2 * SG_GUARD_LEN);
		if (sb->sb_bufs[i] == NULL)
			goto nomem;
		(void) memset(sb->sb_bufs[i], SG_GUARD_BYTE, SG_GUARD_LEN);
		(void) memset(SG_DATA(sb, i), SG_FILL_BYTE, len);
		(void) memset(SG_DATA(sb, i) + len, SG_GUARD_BYTE,
		    SG_GUARD_LEN);
		sum += len;
	}

	sb->sb_cd.cd_format = cryptotest_sg_format;
	sb->sb_cd.cd_offset = SG_PREFIX;
	sb->sb_cd.cd_length = space;

	switch (cryptotest_sg_format) {
	case CRYPTO_DATA_UIO:
		sb->sb_iov = calloc(n, sizeof (iovec_t));
		if (sb->sb_iov == NULL)
			goto nomem;
		for (i = 0; i < n; i++) {
			sb->sb_iov[i].iov_base = (caddr_t)SG_DATA(sb, i);
			sb->sb_iov[i].iov_len = sb->sb_lens[i];
		}
		sb->sb_uio.uio_iov = sb->sb_iov;
		sb->sb_uio.uio_iovcnt = (int)n;
		sb->sb_uio.uio_segflg = UIO_SYSSPACE;
		sb->sb_uio.uio_resid = (ssize_t)total;
		sb->sb_cd.cd_uio = &sb->sb_uio;
		break;

	case CRYPTO_DATA_MBLK:
		sb->sb_mblks = calloc(n, sizeof (mblk_t));
		if (sb->sb_mblks == NULL)
			goto nomem;
		for (i = 0; i < n; i++) {
			mblk_t *mp = &sb->sb_mblks[i];

			mp->b_rptr = SG_DATA(sb, i);
			mp->b_wptr = mp->b_rptr + sb->sb_lens[i];
			mp->b_cont = (i < n - 1) ? mp + 1 : NULL;
		}
		sb->sb_cd.cd_mp = sb->sb_mblks;
		break;

	default:
		(void) fprintf(stderr, "unsupported output format %d\n",
		    cryptotest_sg_format);
		sg_free(sb);
		return (CRYPTO_ARGUMENTS_BAD);
	}

	return (CRYPTO_SUCCESS);

nomem:
	(void) fprintf(stderr, "out of memory for output buffer\n");
	sg_free(sb);
	return (CRYPTO_HOST_MEMORY);
}

/*
 * Get the number of bytes written to the output, according to how far
 * the mode code advanced cd_offset.
 */
static int
sg_produced(sg_buf_t *sb, const char *what, size_t *producedp)
{
	off_t off = sb->sb_cd.cd_offset;

	if (off < SG_PREFIX || (size_t)(off - SG_PREFIX) > sb->sb_space) {
		(void) fprintf(stderr,
		    "%s: output offset %lld out of range [%d, %zu]\n",
		    what, (longlong_t)off, SG_PREFIX,
		    SG_PREFIX + sb->sb_space);
		return (CRYPTO_GENERAL_ERROR);
	}

	*producedp = (size_t)(off - SG_PREFIX);
	return (CRYPTO_SUCCESS);
}

/*
 * Verify that the guard bytes are intact and that nothing outside the
 * expected output range was modified, then gather the output into dst.
 */
static int
sg_check(sg_buf_t *sb, const char *what, uint8_t *dst, size_t *producedp)
{
	size_t produced, pos, len, j;
	size_t nstray = 0;
	uint_t i;
	uint8_t *p;
	int rv;

	*producedp = 0;
	if ((rv = sg_produced(sb, what, &produced)) != CRYPTO_SUCCESS)
		return (rv);

	for (i = 0, pos = 0; i < sb->sb_nsegs; i++) {
		p = sb->sb_bufs[i];
		len = sb->sb_lens[i];

		for (j = 0; j < SG_GUARD_LEN; j++) {
			if (p[j] != SG_GUARD_BYTE ||
			    p[SG_GUARD_LEN + len + j] != SG_GUARD_BYTE) {
				(void) fprintf(stderr,
				    "%s: guard of segment %u (length %zu) "
				    "overwritten\n", what, i, len);
				rv = CRYPTO_GENERAL_ERROR;
				break;
			}
		}

		p = SG_DATA(sb, i);
		for (j = 0; j < len; j++, pos++) {
			if (pos >= SG_PREFIX && pos < SG_PREFIX + produced)
				dst[pos - SG_PREFIX] = p[j];
			else if (p[j] != SG_FILL_BYTE)
				nstray++;
		}
	}

	if (nstray != 0) {
		(void) fprintf(stderr, "%s: %zu bytes outside the output "
		    "range [%d, %zu) were modified\n", what, nstray,
		    SG_PREFIX, SG_PREFIX + produced);
		rv = CRYPTO_GENERAL_ERROR;
	}

	if (rv == CRYPTO_SUCCESS)
		*producedp = produced;
	return (rv);
}

static aes_ctx_t *
sg_alloc_ctx(uint32_t mode)
{
	switch (mode) {
	case ECB_MODE:
		return (ecb_alloc_ctx(0));
	case CBC_MODE:
		return (cbc_alloc_ctx(0));
	case CFB_MODE:
		return (cfb_alloc_ctx(0));
	case CTR_MODE:
		return (ctr_alloc_ctx(0));
	case CCM_MODE:
		return (ccm_alloc_ctx(0));
	case GCM_MODE:
		return (gcm_alloc_ctx(0));
	}
	return (NULL);
}

/* Compare with soft_aes_free_ctx() in pkcs11_softtoken. */
static void
sg_free_ctx(aes_ctx_t *ctx)
{
	if (ctx == NULL)
		return;

	if (ctx->ac_flags & CCM_MODE)
		free(ctx->acu.acu_ccm.ccm_pt_buf);
	else if (ctx->ac_flags & GCM_MODE)
		free(ctx->acu.acu_gcm.gcm_pt_buf);
	free(ctx->ac_keysched);
	free(ctx);
}

/* Mirror the parameter checks done by the aes kernel module. */
static int
sg_check_param(crypto_op_t *op)
{
	size_t len;

	switch (op->mode) {
	case ECB_MODE:
		return (CRYPTO_SUCCESS);
	case CBC_MODE:
	case CFB_MODE:
		len = AES_BLOCK_LEN;
		break;
	case CTR_MODE:
		len = sizeof (CK_AES_CTR_PARAMS);
		break;
	case CCM_MODE:
		len = sizeof (CK_AES_CCM_PARAMS);
		break;
	case GCM_MODE:
		len = sizeof (CK_AES_GCM_PARAMS);
		break;
	default:
		return (CRYPTO_MECHANISM_INVALID);
	}

	if (op->param == NULL || op->paramlen != len)
		return (CRYPTO_MECHANISM_PARAM_INVALID);
	return (CRYPTO_SUCCESS);
}

static int
sg_init(crypto_op_t *op)
{
	CK_AES_CTR_PARAMS *ctrp;
	aes_ctx_t *ctx;
	size_t kslen;
	void *ks;
	int rv;

	/* The harness reuses the op for each multi-part update size. */
	sg_free_ctx(op->ctx);
	op->ctx = NULL;

	if (op->keylen != 16 && op->keylen != 24 && op->keylen != 32) {
		rv = CRYPTO_KEY_SIZE_RANGE;
		goto out;
	}

	if ((rv = sg_check_param(op)) != CRYPTO_SUCCESS)
		goto out;

	if ((ctx = sg_alloc_ctx(op->mode)) == NULL) {
		rv = CRYPTO_HOST_MEMORY;
		goto out;
	}

	if ((ks = aes_alloc_keysched(&kslen, 0)) == NULL) {
		free(ctx);
		rv = CRYPTO_HOST_MEMORY;
		goto out;
	}
	aes_init_keysched(op->key, (uint_t)(op->keylen * 8), ks);
	ctx->ac_keysched = ks;
	ctx->ac_keysched_len = kslen;
	op->ctx = ctx;

	switch (op->mode) {
	case ECB_MODE:
		break;
	case CBC_MODE:
		rv = cbc_init_ctx(&ctx->acu.acu_cbc, op->param, op->paramlen,
		    AES_BLOCK_LEN, aes_copy_block64);
		break;
	case CFB_MODE:
		rv = cfb_init_ctx(&ctx->acu.acu_cfb, op->param, op->paramlen,
		    AES_BLOCK_LEN, aes_copy_block64);
		break;
	case CTR_MODE:
		ctrp = (CK_AES_CTR_PARAMS *)op->param;
		rv = ctr_init_ctx(&ctx->acu.acu_ctr, ctrp->ulCounterBits,
		    ctrp->cb, aes_encrypt_block, aes_copy_block);
		break;
	case CCM_MODE:
		rv = ccm_init_ctx(&ctx->acu.acu_ccm, op->param, 0,
		    (op->fg == CRYPTO_FG_ENCRYPT) ? B_TRUE : B_FALSE,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block);
		break;
	case GCM_MODE:
		rv = gcm_init_ctx(&ctx->acu.acu_gcm, op->param,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block);
		break;
	}

out:
	if (rv != CRYPTO_SUCCESS) {
		char errbuf[BUFSZ] = { 0 };

		(void) fprintf(stderr, "init: Error: %s\n",
		    cryptotest_errstr(rv, errbuf, sizeof (errbuf)));
	}
	return (rv);
}

static int
sg_cipher(crypto_op_t *op, uint8_t *in, size_t len, crypto_data_t *cd)
{
	if (op->fg == CRYPTO_FG_ENCRYPT)
		return (aes_encrypt_contiguous_blocks(op->ctx, (char *)in, len,
		    cd));
	return (aes_decrypt_contiguous_blocks(op->ctx, (char *)in, len, cd));
}

/*
 * Mirror aes_encrypt_final() and aes_decrypt_final() in the aes kernel
 * module.  As there, cd_length is the space available after cd_offset.
 */
static int
sg_cipher_final(crypto_op_t *op, crypto_data_t *cd)
{
	aes_ctx_t *ctx = op->ctx;

	if (op->fg == CRYPTO_FG_ENCRYPT) {
		if (ctx->ac_flags & CCM_MODE) {
			return (ccm_encrypt_final(&ctx->acu.acu_ccm, cd,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block));
		}
		if (ctx->ac_flags & GCM_MODE) {
			return (gcm_encrypt_final(&ctx->acu.acu_gcm, cd,
			    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
			    aes_xor_block));
		}
		if ((ctx->ac_flags & CTR_MODE) == 0 &&
		    ctx->ac_remainder_len > 0)
			return (CRYPTO_DATA_LEN_RANGE);
		return (CRYPTO_SUCCESS);
	}

	if ((ctx->ac_flags & CTR_MODE) == 0 && ctx->ac_remainder_len > 0)
		return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);
	if (ctx->ac_flags & CCM_MODE) {
		return (ccm_decrypt_final(&ctx->acu.acu_ccm, cd,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_copy_block,
		    aes_xor_block));
	}
	if (ctx->ac_flags & GCM_MODE) {
		return (gcm_decrypt_final(&ctx->acu.acu_gcm, cd,
		    AES_BLOCK_LEN, aes_encrypt_block, aes_xor_block));
	}
	return (CRYPTO_SUCCESS);
}

static int
sg_single(crypto_op_t *op)
{
	sg_buf_t sb;
	size_t produced;
	int rv;

	if ((rv = sg_alloc(&sb, op->outlen)) != CRYPTO_SUCCESS)
		return (rv);

	rv = sg_cipher(op, op->in, op->inlen, &sb.sb_cd);
	if (rv == CRYPTO_SUCCESS)
		rv = sg_produced(&sb, "single", &produced);
	if (rv == CRYPTO_SUCCESS) {
		/* Finish in the same output, wherever the cipher left off. */
		sb.sb_cd.cd_length = op->outlen - produced;
		rv = sg_cipher_final(op, &sb.sb_cd);
	}
	if (rv == CRYPTO_SUCCESS)
		rv = sg_check(&sb, "single", op->out, &produced);

	sg_free(&sb);
	return (rv);
}

static int
sg_update(crypto_op_t *op, size_t offset, size_t len, size_t *outlen)
{
	sg_buf_t sb;
	size_t produced;
	int rv;

	if ((rv = sg_alloc(&sb, op->outlen - *outlen)) != CRYPTO_SUCCESS)
		return (rv);

	rv = sg_cipher(op, op->in + offset, len, &sb.sb_cd);
	if (rv != CRYPTO_SUCCESS) {
		char errbuf[BUFSZ] = { 0 };

		(void) fprintf(stderr, "update: Error: %s\n",
		    cryptotest_errstr(rv, errbuf, sizeof (errbuf)));
	} else {
		rv = sg_check(&sb, "update", op->out + *outlen, &produced);
		*outlen += produced;
	}

	sg_free(&sb);
	return (rv);
}

static int
sg_final(crypto_op_t *op, size_t outlen)
{
	sg_buf_t sb;
	size_t produced;
	int rv;

	if ((rv = sg_alloc(&sb, op->outlen - outlen)) != CRYPTO_SUCCESS)
		return (rv);

	rv = sg_cipher_final(op, &sb.sb_cd);
	if (rv == CRYPTO_SUCCESS)
		rv = sg_check(&sb, "final", op->out + outlen, &produced);

	sg_free(&sb);
	return (rv);
}

crypto_op_t *
cryptotest_init(cryptotest_t *arg, crypto_func_group_t fg)
{
	crypto_op_t *op;

	if (arg->out == NULL) {
		(void) fprintf(stderr, "no output buffer given\n");
		return (NULL);
	}

	if ((op = calloc(1, sizeof (*op))) == NULL) {
		(void) fprintf(stderr, "calloc failed\n");
		return (NULL);
	}

	op->in = arg->in;
	op->out = arg->out;
	op->key = arg->key;
	op->param = arg->param;

	op->inlen = arg->inlen;
	op->outlen = arg->outlen;
	op->keylen = arg->keylen;
	op->paramlen = arg->plen;

	op->mechname = arg->mechname;
	op->fg = fg;

	return (op);
}

void
cryptotest_close(crypto_op_t *op)
{
	if (op == NULL)
		return;
	sg_free_ctx(op->ctx);
	free(op);
}

int
get_mech_info(crypto_op_t *op)
{
	uint_t i;

	for (i = 0; i < sizeof (sg_mechs) / sizeof (sg_mechs[0]); i++) {
		if (strcmp(op->mechname, sg_mechs[i].sm_name) == 0) {
			op->mode = sg_mechs[i].sm_mode;
			return (CRYPTO_SUCCESS);
		}
	}

	(void) fprintf(stderr, "failed to resolve mechanism name %s\n",
	    op->mechname);
	return (CTEST_NAME_RESOLVE_FAILED);
}

int
get_hsession_by_mech(crypto_op_t *op)
{
	if (op->fg != CRYPTO_FG_ENCRYPT && op->fg != CRYPTO_FG_DECRYPT) {
		(void) fprintf(stderr, "only encrypt and decrypt are "
		    "supported\n");
		return (CTEST_MECH_NO_PROVIDER);
	}
	return (CRYPTO_SUCCESS);
}

/*
 * CRYPTO_MAC_* functions (not supported)
 */
int
mac_init(crypto_op_t *op __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}

int
mac_single(crypto_op_t *op __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}

int
mac_update(crypto_op_t *op __unused, size_t offset __unused,
    size_t len __unused, size_t *dummy __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}

int
mac_final(crypto_op_t *op __unused, size_t dummy __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}

/*
 * CRYPTO_ENCRYPT_* functions
 */
int
encrypt_init(crypto_op_t *op)
{
	return (sg_init(op));
}

int
encrypt_single(crypto_op_t *op)
{
	return (sg_single(op));
}

int
encrypt_update(crypto_op_t *op, size_t offset, size_t plainlen,
    size_t *encrlen)
{
	return (sg_update(op, offset, plainlen, encrlen));
}

int
encrypt_final(crypto_op_t *op, size_t encrlen)
{
	return (sg_final(op, encrlen));
}

/*
 * CRYPTO_DECRYPT_* functions
 */
int
decrypt_init(crypto_op_t *op)
{
	return (sg_init(op));
}

int
decrypt_single(crypto_op_t *op)
{
	return (sg_single(op));
}

int
decrypt_update(crypto_op_t *op, size_t offset, size_t cipherlen,
    size_t *encrlen)
{
	return (sg_update(op, offset, cipherlen, encrlen));
}

int
decrypt_final(crypto_op_t *op, size_t encrlen)
{
	return (sg_final(op, encrlen));
}

/*
 * CRYPTO_DIGEST_* functions (not supported)
 */
int
digest_init(crypto_op_t *op __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}

int
digest_single(crypto_op_t *op __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}

int
digest_update(crypto_op_t *op __unused, size_t offset __unused,
    size_t len __unused, size_t *dummy __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}

int
digest_final(crypto_op_t *op __unused, size_t dummy __unused)
{
	return (CRYPTO_NOT_SUPPORTED);
}
