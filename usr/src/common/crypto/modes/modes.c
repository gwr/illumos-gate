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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2026 RackTop Systems, Inc.
 */

#ifndef _KERNEL
#include <stdlib.h>
#include <assert.h>
#include <strings.h>
#endif

#include <sys/strsun.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <modes/modes.h>
#include <sys/crypto/common.h>
#include <sys/crypto/impl.h>

/*
 * Initialize by setting iov_or_mp to point to the current iovec or mp,
 * and by setting current_offset to an offset within the current iovec or mp.
 */
void
crypto_init_ptrs(crypto_data_t *out, void **iov_or_mp, offset_t *current_offset)
{
	offset_t offset;

	switch (out->cd_format) {
	case CRYPTO_DATA_RAW:
		*current_offset = out->cd_offset;
		break;

	case CRYPTO_DATA_UIO: {
		uio_t *uiop = out->cd_uio;
		uintptr_t vec_idx;

		offset = out->cd_offset;
		for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
		    offset >= uiop->uio_iov[vec_idx].iov_len;
		    offset -= uiop->uio_iov[vec_idx++].iov_len)
			;

		*current_offset = offset;
		*iov_or_mp = (void *)vec_idx;
		break;
	}

	case CRYPTO_DATA_MBLK: {
		mblk_t *mp;

		offset = out->cd_offset;
		for (mp = out->cd_mp; mp != NULL && offset >= MBLKL(mp);
		    offset -= MBLKL(mp), mp = mp->b_cont)
			;

		*current_offset = offset;
		*iov_or_mp = mp;
		break;

	}
	} /* end switch */
}

/*
 * Get a pointer to the next contiguous span of the output, at most 'amt'
 * bytes long, and advance the output position past that span.  The
 * iov_or_mp argument stores the current iovec index or mblk, and
 * current_offset stores the offset within it (see crypto_init_ptrs).
 *
 * The span returned in *out_data / *out_data_len is limited to what fits
 * in the current iovec, mblk, or raw buffer, so it may be shorter than
 * 'amt'.  Callers that need to write more than *out_data_len bytes must
 * call this again (or use crypto_put_ptrs) for the rest; the output may
 * be split across any number of segments.  When the output has no space
 * left, *out_data is set to NULL and *out_data_len to zero.
 */
void
crypto_get_ptrs(crypto_data_t *out, void **iov_or_mp, offset_t *current_offset,
    uint8_t **out_data, size_t *out_data_len, size_t amt)
{
	offset_t offset = *current_offset;
	uint8_t *base = NULL;
	size_t len = 0;
	size_t span;

	*out_data = NULL;
	*out_data_len = 0;

	switch (out->cd_format) {
	case CRYPTO_DATA_RAW:
		base = (uint8_t *)out->cd_raw.iov_base;
		len = out->cd_raw.iov_len;
		break;

	case CRYPTO_DATA_UIO: {
		uio_t *uio = out->cd_uio;
		uintptr_t vec_idx = (uintptr_t)*iov_or_mp;

		/* Skip past any exhausted (or empty) iovecs. */
		while (vec_idx < uio->uio_iovcnt &&
		    (size_t)offset >= uio->uio_iov[vec_idx].iov_len) {
			offset -= uio->uio_iov[vec_idx].iov_len;
			vec_idx++;
		}
		*iov_or_mp = (void *)vec_idx;
		if (vec_idx < uio->uio_iovcnt) {
			base = (uint8_t *)uio->uio_iov[vec_idx].iov_base;
			len = uio->uio_iov[vec_idx].iov_len;
		}
		break;
	}

	case CRYPTO_DATA_MBLK: {
		mblk_t *mp = (mblk_t *)*iov_or_mp;

		/* Skip past any exhausted (or empty) mblks. */
		while (mp != NULL && offset >= MBLKL(mp)) {
			offset -= MBLKL(mp);
			mp = mp->b_cont;
		}
		*iov_or_mp = mp;
		if (mp != NULL) {
			base = mp->b_rptr;
			len = MBLKL(mp);
		}
		break;
	}
	} /* end switch */

	*current_offset = offset;
	if (offset < 0 || (size_t)offset >= len)
		return;

	span = MIN(amt, len - (size_t)offset);
	*out_data = base + offset;
	*out_data_len = span;
	*current_offset = offset + span;
}

/*
 * Copy 'len' bytes from 'src' to the output at the position tracked by
 * iov_or_mp and current_offset, spanning as many iovecs or mblks as needed,
 * and advance the position.  If copy_block is not NULL and the next span
 * of the output can hold all 'len' bytes (which must then be one cipher
 * block), copy_block is used for the copy.
 *
 * The callers' providers check the declared output length (cd_length)
 * before any processing, so running out of space here means cd_length
 * claims more room than the iovecs or mblks actually provide.  Mode state
 * may already have been advanced, so the operation cannot be retried;
 * fail with CRYPTO_ARGUMENTS_BAD rather than CRYPTO_BUFFER_TOO_SMALL.
 */
int
crypto_put_ptrs(crypto_data_t *out, void **iov_or_mp, offset_t *current_offset,
    uint8_t *src, size_t len, void (*copy_block)(uint8_t *, uint8_t *))
{
	uint8_t *dst;
	size_t dst_len;

	while (len > 0) {
		crypto_get_ptrs(out, iov_or_mp, current_offset, &dst, &dst_len,
		    len);
		if (dst_len == 0)
			return (CRYPTO_ARGUMENTS_BAD);

		if (copy_block != NULL && dst_len == len)
			copy_block(src, dst);
		else
			bcopy(src, dst, dst_len);

		copy_block = NULL;
		src += dst_len;
		len -= dst_len;
	}

	return (CRYPTO_SUCCESS);
}

void
crypto_free_mode_ctx(void *ctx)
{
	common_ctx_t *common_ctx = (common_ctx_t *)ctx;

	switch (common_ctx->cc_flags & (ECB_MODE|CBC_MODE|CMAC_MODE|CTR_MODE|
	    CFB_MODE|CCM_MODE|GCM_MODE|GMAC_MODE)) {
	case ECB_MODE:
#ifdef _KERNEL
		kmem_free(common_ctx, sizeof (ecb_ctx_t));
#else
		free(common_ctx);
#endif
		break;

	case CFB_MODE:
#ifdef _KERNEL
		kmem_free(common_ctx, sizeof (cfb_ctx_t));
#else
		free(common_ctx);
#endif
		break;

	case CBC_MODE:
	case CMAC_MODE:
#ifdef _KERNEL
		kmem_free(common_ctx, sizeof (cbc_ctx_t));
#else
		free(common_ctx);
#endif
		break;

	case CTR_MODE:
#ifdef _KERNEL
		kmem_free(common_ctx, sizeof (ctr_ctx_t));
#else
		free(common_ctx);
#endif
		break;

	case CCM_MODE:
#ifdef _KERNEL
		if (((ccm_ctx_t *)ctx)->ccm_pt_buf != NULL)
			kmem_free(((ccm_ctx_t *)ctx)->ccm_pt_buf,
			    ((ccm_ctx_t *)ctx)->ccm_data_len);

		kmem_free(ctx, sizeof (ccm_ctx_t));
#else
		if (((ccm_ctx_t *)ctx)->ccm_pt_buf != NULL)
			free(((ccm_ctx_t *)ctx)->ccm_pt_buf);
		free(ctx);
#endif
		break;

	case GCM_MODE:
	case GMAC_MODE:
#ifdef _KERNEL
		if (((gcm_ctx_t *)ctx)->gcm_pt_buf != NULL)
			kmem_free(((gcm_ctx_t *)ctx)->gcm_pt_buf,
			    ((gcm_ctx_t *)ctx)->gcm_pt_buf_len);

		kmem_free(ctx, sizeof (gcm_ctx_t));
#else
		if (((gcm_ctx_t *)ctx)->gcm_pt_buf != NULL)
			free(((gcm_ctx_t *)ctx)->gcm_pt_buf);
		free(ctx);
#endif
	}
}

/*
 * Utility routine to apply the command, 'cmd', to the
 * data in the uio structure.
 */
int
crypto_uio_data(crypto_data_t *data, uchar_t *buf, int len, cmd_type_t cmd,
    void *digest_ctx, void (*update)())
{
	uio_t *uiop = data->cd_uio;
	off_t offset = data->cd_offset;
	size_t length = len;
	uint_t vec_idx;
	size_t cur_len;
	uchar_t *datap;

#ifdef _KERNEL
	ASSERT3U(data->cd_format, ==, CRYPTO_DATA_UIO);
#else
	assert(data->cd_format == CRYPTO_DATA_UIO);
#endif
	if (uiop->uio_segflg != UIO_SYSSPACE) {
		return (CRYPTO_ARGUMENTS_BAD);
	}

	/*
	 * Jump to the first iovec containing data to be
	 * processed.
	 */
	for (vec_idx = 0; vec_idx < uiop->uio_iovcnt &&
	    offset >= uiop->uio_iov[vec_idx].iov_len;
	    offset -= uiop->uio_iov[vec_idx++].iov_len)
		;

	if (vec_idx == uiop->uio_iovcnt && length > 0) {
		/*
		 * The caller specified an offset that is larger than
		 * the total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	while (vec_idx < uiop->uio_iovcnt && length > 0) {
		cur_len = MIN(uiop->uio_iov[vec_idx].iov_len -
		    offset, length);

		datap = (uchar_t *)(uiop->uio_iov[vec_idx].iov_base +
		    offset);
		switch (cmd) {
		case COPY_FROM_DATA:
			bcopy(datap, buf, cur_len);
			buf += cur_len;
			break;
		case COPY_TO_DATA:
			bcopy(buf, datap, cur_len);
			buf += cur_len;
			break;
		case COMPARE_TO_DATA:
			if (bcmp(datap, buf, cur_len))
				return (CRYPTO_SIGNATURE_INVALID);
			buf += cur_len;
			break;
		case MD5_DIGEST_DATA:
		case SHA1_DIGEST_DATA:
		case SHA2_DIGEST_DATA:
		case GHASH_DATA:
			update(digest_ctx, datap, cur_len);
			break;
		}

		length -= cur_len;
		vec_idx++;
		offset = 0;
	}

	if (vec_idx == uiop->uio_iovcnt && length > 0) {
		/*
		 * The end of the specified iovec's was reached but
		 * the length requested could not be processed.
		 */
		switch (cmd) {
		case COPY_TO_DATA:
			data->cd_length = len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		default:
			return (CRYPTO_DATA_LEN_RANGE);
		}
	}

	return (CRYPTO_SUCCESS);
}

/*
 * Utility routine to apply the command, 'cmd', to the
 * data in the mblk structure.
 */
int
crypto_mblk_data(crypto_data_t *data, uchar_t *buf, int len, cmd_type_t cmd,
    void *digest_ctx, void (*update)())
{
	off_t offset = data->cd_offset;
	size_t length = len;
	mblk_t *mp;
	size_t cur_len;
	uchar_t *datap;

#ifdef _KERNEL
	ASSERT3U(data->cd_format, ==, CRYPTO_DATA_MBLK);
#else
	assert(data->cd_format == CRYPTO_DATA_MBLK);
#endif
	/*
	 * Jump to the first mblk_t containing data to be processed.
	 */
	for (mp = data->cd_mp; mp != NULL && offset >= MBLKL(mp);
	    offset -= MBLKL(mp), mp = mp->b_cont)
		;
	if (mp == NULL) {
		/*
		 * The caller specified an offset that is larger
		 * than the total size of the buffers it provided.
		 */
		return (CRYPTO_DATA_LEN_RANGE);
	}

	/*
	 * Now do the processing on the mblk chain.
	 */
	while (mp != NULL && length > 0) {
		cur_len = MIN(MBLKL(mp) - offset, length);

		datap = (uchar_t *)(mp->b_rptr + offset);
		switch (cmd) {
		case COPY_FROM_DATA:
			bcopy(datap, buf, cur_len);
			buf += cur_len;
			break;
		case COPY_TO_DATA:
			bcopy(buf, datap, cur_len);
			buf += cur_len;
			break;
		case COMPARE_TO_DATA:
			if (bcmp(datap, buf, cur_len))
				return (CRYPTO_SIGNATURE_INVALID);
			buf += cur_len;
			break;
		case MD5_DIGEST_DATA:
		case SHA1_DIGEST_DATA:
		case SHA2_DIGEST_DATA:
		case GHASH_DATA:
			update(digest_ctx, datap, cur_len);
			break;
		}

		length -= cur_len;
		offset = 0;
		mp = mp->b_cont;
	}

	if (mp == NULL && length > 0) {
		/*
		 * The end of the mblk was reached but the length
		 * requested could not be processed.
		 */
		switch (cmd) {
		case COPY_TO_DATA:
			data->cd_length = len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		default:
			return (CRYPTO_DATA_LEN_RANGE);
		}
	}

	return (CRYPTO_SUCCESS);
}

/*
 * Utility routine to copy a buffer to a crypto_data structure.
 */
int
crypto_put_output_data(uchar_t *buf, crypto_data_t *output, int len)
{
	switch (output->cd_format) {
	case CRYPTO_DATA_RAW:
		if (MAXOFF_T - output->cd_offset < (off_t)len) {
			return (CRYPTO_ARGUMENTS_BAD);
		}
		if (output->cd_raw.iov_len < len + output->cd_offset) {
			output->cd_length = len;
			return (CRYPTO_BUFFER_TOO_SMALL);
		}
		bcopy(buf, (uchar_t *)(output->cd_raw.iov_base +
		    output->cd_offset), len);
		break;

	case CRYPTO_DATA_UIO:
		return (crypto_uio_data(output, buf, len,
		    COPY_TO_DATA, NULL, NULL));

	case CRYPTO_DATA_MBLK:
		return (crypto_mblk_data(output, buf, len,
		    COPY_TO_DATA, NULL, NULL));

	default:
		return (CRYPTO_ARGUMENTS_BAD);
	}

	return (CRYPTO_SUCCESS);
}
