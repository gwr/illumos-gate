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
 * Copyright 2021 RackTop Systems, Inc.
 */

#include <sys/param.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/sdt.h>

#include <netsmb/smb_osdep.h>

#include <netsmb/smb.h>
#include <netsmb/smb2.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb3_negctx.h>
#include <netsmb/mchain.h>

#include <fs/smbcrypt/smb_kcrypt.h>

/*
 * This function should be called only for dialect >= 0x311
 * Negotiate context list should contain exactly one
 * SMB2_PREAUTH_INTEGRITY_CAPS context.
 * Otherwise STATUS_INVALID_PARAMETER.
 * It should contain at least 1 hash algorith what server does support.
 * Otehrwise STATUS_SMB_NO_PREAUTH_INEGRITY_HASH_OVERLAP.
 *
 * SMB 3.1.1 specifies the only hashing algorithm - SHA-512.
 * Currently, there are two encryption algorithms:
 * AES-128-CCM, AES-128-GCM
 */
int xxx = 0;
uint32_t
smb31_decode_negctxs(struct smb_vc *vcp, struct mdchain *mdp,
    smb2_neg_ctxs_t *neg_ctxs)
{
	smb2_preauth_caps_t *picap = &neg_ctxs->preauth_ctx.preauth_caps;
	smb2_encrypt_caps_t *encap = &neg_ctxs->encrypt_ctx.encrypt_caps;
	boolean_t preauth_sha512_enabled = B_FALSE;
	boolean_t encrypt_ccm128_enabled = B_FALSE;
	boolean_t encrypt_gcm128_enabled = B_FALSE;
	/* XXX: use clnt config. */
	uint16_t cipher = xxx ? SMB3_CIPHER_AES128_CCM : SMB3_CIPHER_AES128_GCM;
	uint32_t status = 0;
	int found_preauth_ctx = 0;
	int found_encrypt_ctx = 0;
	int rc;

	DTRACE_PROBE2(xxx_cnt2, int, neg_ctxs->offset, int, neg_ctxs->count);
	/*
	 * There should be exactly 1 SMB2_PREAUTH_INTEGRITY_CAPS negotiate ctx.
	 * SMB2_ENCRYPTION_CAPS is optional one.
	 * If there is no contexts or there are to many then stop parsing.
	 */
	if (neg_ctxs->count < 1 || neg_ctxs->count > NEG_CTX_MAX_COUNT) {
		status = NT_STATUS_INVALID_PARAMETER;
		goto errout;
	}

	/*
	 * Cannot proceed parsing if the first context isn't aligned by 8.
	 */
	if (neg_ctxs->offset % 8 != 0) {
		status = NT_STATUS_INVALID_PARAMETER;
		goto errout;
	}
#ifdef	_KERNEL
	uint32_t pos = md_tell(mdp);
	ASSERT3U(pos, ==, neg_ctxs->offset);
#endif
	/*
	 * Parse negotiate contexts. Ignore non-decoding errors to fill
	 * as much as possible data for dtrace probe.
	 */
	for (int i = 0; i < neg_ctxs->count; i++) {
		smb2_neg_ctx_t neg_ctx;
		int32_t ctx_end_off;
		int32_t ctx_next_off;
		int skip;

		if (i > 0) {
			if ((skip = ctx_next_off - ctx_end_off) != 0 &&
				md_get_mem(mdp, NULL, skip, MB_MINLINE) != 0) {
				status = NT_STATUS_INVALID_PARAMETER;
				goto errout;
			}
		}

		rc = md_get_uint16le(mdp, &neg_ctx.type);
		if (rc != 0) {
			status = NT_STATUS_INVALID_PARAMETER;
			goto errout;
		}

		rc = md_get_uint16le(mdp, &neg_ctx.datalen);
		if (rc != 0) {
			status = NT_STATUS_INVALID_PARAMETER;
			goto errout;
		}

		md_get_uint32le(mdp, NULL);	/* reserved */
		DTRACE_PROBE2(xxx_ctx, int ,neg_ctx.type, int ,neg_ctx.datalen);
		/*
		 * We got something crazy
		 */
		if (neg_ctx.datalen > NEG_CTX_MAX_DATALEN) {
			status = NT_STATUS_INVALID_PARAMETER;
			goto errout;
		}

		ctx_end_off = md_tell(mdp) + neg_ctx.datalen;
		ctx_next_off = P2ROUNDUP(ctx_end_off, 8);

		switch (neg_ctx.type) {
		case SMB2_PREAUTH_INTEGRITY_CAPS:
			memcpy(&neg_ctxs->preauth_ctx.neg_ctx, &neg_ctx,
			    sizeof (neg_ctx));

			if (found_preauth_ctx++ != 0) {
				status = NT_STATUS_INVALID_PARAMETER;
				continue;
			}

			rc = md_get_uint16le(mdp, &picap->picap_hash_count);
			if (rc != 0) {
				status = NT_STATUS_INVALID_PARAMETER;
				goto errout;
			}

			rc = md_get_uint16le(mdp, &picap->picap_salt_len);
			if (rc != 0) {
				status = NT_STATUS_INVALID_PARAMETER;
				goto errout;
			}

			/*
			 * Get hash id
			 */
			/* For now the code ignores hash count - 1 is assumed */
			rc = md_get_uint16le(mdp, &picap->picap_hash_id);
			if (rc != 0) {
				status = NT_STATUS_INVALID_PARAMETER;
				goto errout;
			}

			/*
			 * Get salt
			 */
			rc = md_get_mem(mdp, &picap->picap_salt, 
			    sizeof (picap->picap_salt), MB_MINLINE);
			if (rc != 0) {
				status = NT_STATUS_INVALID_PARAMETER;
				goto errout;
			}

			/*
			 * In SMB 0x311 there should be exactly 1 preauth
			 * negotiate context, and there should be exactly 1
			 * hash value in the list - SHA512.
			 */
			DTRACE_PROBE1(xxx_integr, int, picap->picap_hash_count);
			if (picap->picap_hash_count != 1) {
				status = NT_STATUS_INVALID_PARAMETER;
				continue;
			}

			if (picap->picap_hash_id == SMB3_HASH_SHA512)
				preauth_sha512_enabled = B_TRUE;
			break;
		case SMB2_ENCRYPTION_CAPS:
			memcpy(&neg_ctxs->preauth_ctx.neg_ctx, &neg_ctx,
			    sizeof (neg_ctx));

			if (found_encrypt_ctx++ != 0) {
				status = NT_STATUS_INVALID_PARAMETER;
				continue;
			}

			rc = md_get_uint16le(mdp, &encap->encap_cipher_count);
			if (rc != 0 || encap->encap_cipher_count >
			    MAX_CIPHER_NUM) {
				status = NT_STATUS_INVALID_PARAMETER;
				goto errout;
			}

			/*
			 * Get cipher list
			 */
			DTRACE_PROBE1(xxx_encr, int, encap->encap_cipher_count);
			for (int k = 0; k < encap->encap_cipher_count; k++) {
				rc = md_get_uint16le(mdp,
				    &encap->encap_cipher_ids[k]);
				cmn_err(CE_NOTE, "cipher[%d]: %d", k, encap->encap_cipher_ids[k]);
				if (rc != 0) {
					status = NT_STATUS_INVALID_PARAMETER;
					goto errout;
				}
			}

			for (int k = 0; k < encap->encap_cipher_count; k++) {
				switch (encap->encap_cipher_ids[k]) {
				case SMB3_CIPHER_AES128_GCM:
					encrypt_gcm128_enabled = B_TRUE;
					break;
				case SMB3_CIPHER_AES128_CCM:
					encrypt_ccm128_enabled = B_TRUE;
					break;
				default:
					;
				}
			}
			break;
		default:
			;
		}
	}

	if (status)
		goto errout;

	/* Not found mandatory SMB2_PREAUTH_INTEGRITY_CAPS ctx */
	if (found_preauth_ctx != 1 || found_encrypt_ctx > 1) {
		status = NT_STATUS_INVALID_PARAMETER;
		goto errout;
	}

	if (!preauth_sha512_enabled) {
		status = STATUS_PREAUTH_HASH_OVERLAP;
		goto errout;
	}

	vcp->vc3_preauth_hashid = SMB3_HASH_SHA512;

	switch (cipher) {
	case SMB3_CIPHER_AES128_GCM:
		if (encrypt_gcm128_enabled) {
			vcp->vc3_enc_cipherid = cipher;
			break;
		}
		/* FALLTHROUGH */
	case SMB3_CIPHER_AES128_CCM:
		if (encrypt_ccm128_enabled) {
			vcp->vc3_enc_cipherid = cipher;
			break;
		}
		/* FALLTHROUGH */
	default:
		vcp->vc3_enc_cipherid = 0;
	}

	DTRACE_PROBE1(xxx_yyy, int, vcp->vc3_enc_cipherid);

errout:
	return (status);
}
