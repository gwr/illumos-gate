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
 * Copyright 2026 RackTop Systems, Inc.
 */

#ifndef _KERNEL
#include <strings.h>
#include <limits.h>
#include <assert.h>
#include <security/cryptoki.h>
#endif

#include <sys/debug.h>
#include <sys/types.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

/*
 * Algorithm independent CFB functions.
 * NOTE: for now, this is CFB8-only.
 * If this is expanded, cfb_ctx_t should probably differentiate between
 * shift lengths (CFB1, CFB8, CFB128, etc).
 */

int
cfb_init_ctx(cfb_ctx_t *cfb_ctx, char *param, size_t param_len,
    size_t block_size, void (*copy_block)(uint8_t *, uint64_t *))
{
	/*
	 * Copy IV into context.
	 *
	 * If cm_param == NULL then the IV comes from the
	 * cd_miscdata field in the crypto_data structure.
	 */
	if (param != NULL) {
#ifdef _KERNEL
		ASSERT(param_len == block_size);
#else
		assert(param_len == block_size);
#endif
		copy_block((uchar_t *)param, cfb_ctx->cfb_iv);
	}

	cfb_ctx->cfb_lastp = (uint8_t *)&cfb_ctx->cfb_iv[0];
	cfb_ctx->cfb_flags |= CFB_MODE;
	return (CRYPTO_SUCCESS);
}

/* ARGSUSED */
void *
cfb_alloc_ctx(int kmflag)
{
	cfb_ctx_t *cfb_ctx;

#ifdef _KERNEL
	if ((cfb_ctx = kmem_zalloc(sizeof (cfb_ctx_t), kmflag)) == NULL)
#else
	if ((cfb_ctx = calloc(1, sizeof (cfb_ctx_t))) == NULL)
#endif
		return (NULL);

	cfb_ctx->cfb_flags = CFB_MODE;
	return (cfb_ctx);
}

int
cfb_mode_contiguous_blocks(cfb_ctx_t *ctx, char *in, size_t in_length,
    crypto_data_t *out, size_t block_size, boolean_t encrypt,
    int (*cipher)(const void *, const uint8_t *, uint8_t *))
{
	uint8_t *stream = (uint8_t *)ctx->cfb_remainder;
	uint8_t *iv = (uint8_t *)ctx->cfb_iv;
	uint8_t *inp = (uint8_t *)in;
	size_t in_remainder = in_length;
	void *iov_or_mp;
	offset_t offset;
	uint8_t *out_data;
	size_t out_data_len;

	if (block_size == 0 || block_size > sizeof (ctx->cfb_remainder))
		return (CRYPTO_ARGUMENTS_BAD);

	if (out == NULL)
		return (CRYPTO_ARGUMENTS_BAD);

	/* Make sure 'out->cd_offset + in_length' doesn't overflow. */
	if (out->cd_offset < 0)
		return (CRYPTO_DATA_LEN_RANGE);
	if (SIZE_MAX - in_length < (size_t)out->cd_offset)
		return (CRYPTO_ENCRYPTED_DATA_LEN_RANGE);

	crypto_init_ptrs(out, &iov_or_mp, &offset);

	while (in_remainder > 0) {
		crypto_get_ptrs(out, &iov_or_mp, &offset, &out_data,
		    &out_data_len, NULL, in_remainder);

		/*
		 * crypto_get_ptrs() should guarantee these, but act as a
		 * safeguard in case the behavior ever changes.
		 */
		ASSERT3U(out_data_len, <=, in_remainder);
		ASSERT3U(out_data_len, >, 0);

		while (out_data_len > 0) {
			uint8_t out_val;
			uint8_t in_val;
			int rv;

			rv = cipher(ctx->cfb_keysched, iv, stream);
			if (rv != CRYPTO_SUCCESS)
				return (rv);

			in_val = *inp++;
			out_val = in_val ^ stream[0];
			*out_data++ = out_val;

			/*
			 * Note that the byte appended to the cipher input is
			 * always the ciphertext of the operation.
			 * For encryption, ciphertext is the output;
			 * for decryption, ciphertext is the input.
			 */
			bcopy(iv + 1, iv, block_size - 1);
			iv[block_size - 1] = encrypt ? out_val : in_val;

			out_data_len--;
			in_remainder--;
		}
	}

	out->cd_offset += in_length;

	return (CRYPTO_SUCCESS);
}
