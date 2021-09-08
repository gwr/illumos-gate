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
 * Copyright 2021-2025 RackTop Systems, Inc.
 */

#include <sys/param.h>
#include <sys/random.h>
#include <sys/sunddi.h>
#include <sys/sysmacros.h>
#include <sys/sdt.h>

#include <netsmb/smb_osdep.h>

#include <netsmb/smb.h>
#include <netsmb/smb2.h>
#include <netsmb/smb_conn.h>
#include <netsmb/mchain.h>

#include <netsmb/nsmb_kcrypt.h>

#define	NEG_CTX_MAX_COUNT	(16)
#define	NEG_CTX_MAX_DATALEN	(256)

enum smb2_neg_ctx_type {
	SMB2_PREAUTH_INTEGRITY_CAPS		= 1,
	SMB2_ENCRYPTION_CAPS			= 2,
	SMB2_COMPRESSION_CAPS			= 3,	/* not implemented */
	SMB2_NETNAME_NEGOTIATE_CONTEXT_ID	= 5	/* not implemented */
};

typedef struct smb2_negotiate_ctx {
	uint16_t	type;
	uint16_t	datalen;
} smb2_neg_ctx_t;

#define	SMB31_PREAUTH_CTX_SALT_LEN	32

/*
 * SMB 3.1.1 specifies the only hashing algorithm - SHA-512 and
 * two encryption ones - AES-128-CCM and AES-128-GCM.
 */
#define	MAX_HASHID_NUM	(1)
#define	MAX_CIPHER_NUM	(4)

#define	SMB31_PREAUTH_CTX_SALT_LEN	32

typedef struct smb2_preauth_integrity_caps {
	uint16_t	picap_hash_count;
	uint16_t	picap_salt_len;
	uint16_t	picap_hash_id;
	uint8_t		picap_salt[SMB31_PREAUTH_CTX_SALT_LEN];
} smb2_preauth_caps_t;

typedef struct smb2_encryption_caps {
	uint16_t	encap_cipher_count;
	uint16_t	encap_cipher_ids[MAX_CIPHER_NUM];
} smb2_encrypt_caps_t;

/*
 * The contexts we support
 */
typedef struct smb2_preauth_neg_ctx {
	smb2_neg_ctx_t		neg_ctx;
	smb2_preauth_caps_t	preauth_caps;
} smb2_preauth_neg_ctx_t;

typedef struct smb2_encrypt_neg_ctx {
	smb2_neg_ctx_t		neg_ctx;
	smb2_encrypt_caps_t	encrypt_caps;
} smb2_encrypt_neg_ctx_t;

typedef struct smb2_neg_ctxs {
	uint32_t		offset;
	uint16_t		count;
	smb2_preauth_neg_ctx_t	preauth_ctx;
	smb2_encrypt_neg_ctx_t	encrypt_ctx;
} smb2_neg_ctxs_t;


/*
 * SMB 3.1.1 Negotiate Contexts
 */

int nsmb_ciphers_enabled = 0xf;	/* handy for testing */

/*
 * Put neg. contexts
 */
int
smb3_negctxs_encode(struct smb_vc *vcp, struct mbchain *mbp,
	uint32_t *negctx_off_p, uint16_t *negctx_cnt_p)
{

	uint8_t salt[SMB31_PREAUTH_CTX_SALT_LEN];
	size_t saltlen = sizeof (salt);
	int start;
	uint16_t *ccnt_p;
	uint16_t *len_p;
	uint16_t len;
	uint16_t ctx_cnt = 0;
	uint16_t ccnt = 0;

	(void) random_get_pseudo_bytes(salt, saltlen);

	/*
	 * Put Contexts. Align 8 first.
	 */
	mb_put_align8(mbp);
	*negctx_off_p = htolel(mbp->mb_count);

	/*
	 * 2.2.3.1.1 SMB2_PREAUTH_INTEGRITY_CAPABILITIES
	 * This one is _required_ for SMB 3.1.1
	 */
	mb_put_uint16le(mbp, SMB2_PREAUTH_INTEGRITY_CAPS);
	len_p = mb_reserve(mbp, 2);
	mb_put_uint32le(mbp, 0);	/* Reserved */
	start = mbp->mb_count;

	mb_put_uint16le(mbp, 1);	/* HashAlgorithmCount */
	mb_put_uint16le(mbp, saltlen);	/* SaltLength */

	/* The only supported hash algo - SHA-512 */
	mb_put_uint16le(mbp, SMB3_HASH_SHA512);
	/* Salt */
	mb_put_mem(mbp, salt, saltlen, MB_MSYSTEM);

	len = mbp->mb_count - start;
	*len_p = htoles(len);
	ctx_cnt++;

	/*
	 * 2.2.3.1.2 SMB2_ENCRYPTION_CAPABILITIES
	 * List ciphers most preferred first.
	 * Todo: make these configurable
	 */
	mb_put_align8(mbp);

	mb_put_uint16le(mbp, SMB2_ENCRYPTION_CAPS);
	len_p = mb_reserve(mbp, 2);
	mb_put_uint32le(mbp, 0);	/* Reserved */
	start = mbp->mb_count;

	ccnt_p = mb_reserve(mbp, 2);	/* CipherCount */

	/* Ciphers, in order of preference. */

	if (nsmb_ciphers_enabled & 8) {
		mb_put_uint16le(mbp, SMB3_CIPHER_AES256_GCM);
		ccnt++;
	}
	if (nsmb_ciphers_enabled & 4) {
		mb_put_uint16le(mbp, SMB3_CIPHER_AES256_CCM);
		ccnt++;
	}
	if (nsmb_ciphers_enabled & 2) {
		mb_put_uint16le(mbp, SMB3_CIPHER_AES128_GCM);
		ccnt++;
	}
	if (nsmb_ciphers_enabled & 1) {
		mb_put_uint16le(mbp, SMB3_CIPHER_AES128_CCM);
		ccnt++;
	}

	*ccnt_p = htoles(ccnt);

	len = mbp->mb_count - start;
	*len_p = htoles(len);
	ctx_cnt++;

	*negctx_cnt_p = htoles(ctx_cnt);

	return (0);
}


/*
 * This function should be called only for dialect >= 0x311
 * Negotiate context list should contain exactly one
 * SMB2_PREAUTH_INTEGRITY_CAPS context.
 * Otherwise STATUS_INVALID_PARAMETER.
 * It should contain at least 1 hash algorithm what server does support.
 * Otherwise STATUS_SMB_NO_PREAUTH_INEGRITY_HASH_OVERLAP.
 *
 * This returns Unix-style error codes, so any NT-style status codes
 * shown here are just for informational purposes.
 *
 * SMB 3.1.1 specifies the only hashing algorithm - SHA-512.
 * SMB 3.1.1 specifies four encryption algorithms:
 * AES-128-CCM, AES-128-GCM, AES-256-CCM, AES-256-GCM
 */
int
smb3_negctxs_decode(struct smb_vc *vcp, struct mdchain *mdp,
    uint16_t negctx_cnt)
{
	smb2_neg_ctxs_t negctxs_stor;
	smb2_neg_ctxs_t *neg_ctxs = &negctxs_stor;
	smb2_preauth_caps_t *picap = &neg_ctxs->preauth_ctx.preauth_caps;
	smb2_encrypt_caps_t *encap = &neg_ctxs->encrypt_ctx.encrypt_caps;
	uint16_t cipher = 0;
	int found_preauth_ctx = 0;
	int found_encrypt_ctx = 0;
	int err = 0;
	int rc;

	bzero(neg_ctxs, sizeof (*neg_ctxs));
	neg_ctxs->offset = md_tell(mdp);
	neg_ctxs->count = negctx_cnt;

	/*
	 * There should be exactly 1 SMB2_PREAUTH_INTEGRITY_CAPS negotiate ctx.
	 * SMB2_ENCRYPTION_CAPS is optional one.
	 * If there is no contexts or there are too many then stop parsing.
	 */
	if (neg_ctxs->count < 1 || neg_ctxs->count > NEG_CTX_MAX_COUNT) {
		err = EINVAL;
		goto errout;
	}

	/*
	 * Parse negotiate contexts. Ignore non-decoding errors to fill
	 * as much as possible data for dtrace probe.
	 */
	for (int i = 0; i < neg_ctxs->count; i++) {
		smb2_neg_ctx_t neg_ctx;
		uint32_t ctx_start;
		uint32_t ctx_end_off;
		uint32_t ctx_next_off;

		/*
		 * Every context must be 8-byte aligned.
		 */
		ctx_start = md_tell(mdp);
		if ((ctx_start % 8) != 0) {
			err = EINVAL;
			goto errout;
		}

		/*
		 * Get Type, len, and "reserved"
		 */
		if (md_get_uint16le(mdp, &neg_ctx.type) != 0 ||
		    md_get_uint16le(mdp, &neg_ctx.datalen) != 0 ||
		    md_get_uint32le(mdp, NULL) != 0) {
			err = EINVAL;
			goto errout;
		}
		DTRACE_PROBE1(neg_ctx, smb2_neg_ctx_t *, &neg_ctx);

		/*
		 * We got something crazy?
		 */
		if (neg_ctx.datalen > NEG_CTX_MAX_DATALEN) {
			err = EINVAL;
			goto errout;
		}

		/*
		 * Figure out where the next ctx should be.
		 * We're now at position: ctx_start + 8
		 */
		ctx_end_off = ctx_start + 8 + neg_ctx.datalen;
		ctx_next_off = P2ROUNDUP(ctx_end_off, 8);

		switch (neg_ctx.type) {
		case SMB2_PREAUTH_INTEGRITY_CAPS:
			memcpy(&neg_ctxs->preauth_ctx.neg_ctx, &neg_ctx,
			    sizeof (neg_ctx));

			if (found_preauth_ctx++ != 0) {
				err = EINVAL;
				goto errout;
			}

			rc = md_get_uint16le(mdp, &picap->picap_hash_count);
			if (rc != 0) {
				err = EINVAL;
				goto errout;
			}

			rc = md_get_uint16le(mdp, &picap->picap_salt_len);
			if (rc != 0) {
				err = EINVAL;
				goto errout;
			}

			/*
			 * Get hash id.  Only deal with one ID,
			 * though eventually there could be more,
			 * with picap_hash_count of them.
			 */
			rc = md_get_uint16le(mdp, &picap->picap_hash_id);
			if (rc != 0) {
				err = EINVAL;
				goto errout;
			}

			/*
			 * Get salt
			 */
			rc = md_get_mem(mdp, &picap->picap_salt,
			    sizeof (picap->picap_salt), MB_MSYSTEM);
			if (rc != 0) {
				err = EINVAL;
				goto errout;
			}
			break;

		case SMB2_ENCRYPTION_CAPS:
			memcpy(&neg_ctxs->encrypt_ctx.neg_ctx, &neg_ctx,
			    sizeof (neg_ctx));

			if (found_encrypt_ctx++ != 0) {
				err = EINVAL;
				goto errout;
			}

			rc = md_get_uint16le(mdp, &encap->encap_cipher_count);
			if (rc != 0 || encap->encap_cipher_count < 1 ||
			    encap->encap_cipher_count > MAX_CIPHER_NUM) {
				err = EINVAL;
				goto errout;
			}

			/*
			 * Get cipher list.  Should be just one here,
			 * the one the server reply tells us to use.
			 */
			for (int k = 0; k < encap->encap_cipher_count; k++) {
				rc = md_get_uint16le(mdp,
				    &encap->encap_cipher_ids[k]);
				if (rc != 0) {
					err = EINVAL;
					goto errout;
				}
			}
			cipher = encap->encap_cipher_ids[0];
			break;

		default: /* context type ignored */
			break;
		}

		/*
		 * If there's another context after this,
		 * skip any padding that might remain.
		 */
		if ((i + 1) < neg_ctxs->count) {
			int skip = ctx_next_off - md_tell(mdp);
			if (skip < 0) {
				err = EINVAL;
				goto errout;
			}
			if (skip > 0) {
				(void) md_get_mem(mdp, NULL, skip, MB_MSYSTEM);
			}
		}

	}

	if (err != 0)
		goto errout;

	DTRACE_PROBE1(decoded, smb2_neg_ctxs_t *, neg_ctxs);

	/*
	 * In SMB 0x311 there should be exactly 1 preauth
	 * negotiate context, and there should be exactly 1
	 * hash value in the list - SHA512.
	 */
	if (found_preauth_ctx != 1 || picap->picap_hash_count != 1) {
		err = EINVAL;
		goto errout;
	}
	if (picap->picap_hash_id != SMB3_HASH_SHA512) {
		/* STATUS_SMB_NO_PREAUTH_INEGRITY_HASH_OVERLAP */
		err = EAUTH;
		goto errout;
	}
	vcp->vc3_preauth_hashid = SMB3_HASH_SHA512;

	/*
	 * If the reply included an encryption algorithm, use that.
	 * Should be zero or one encrypt context. If none, just
	 * assume we should use the SMB 3.02 alg.
	 */
	if (found_encrypt_ctx == 0) {
		vcp->vc3_enc_cipherid = SMB3_CIPHER_AES128_CCM;
	} else {
		/* Sanity check what we got. */
		switch (cipher) {
		case SMB3_CIPHER_AES256_GCM:
		case SMB3_CIPHER_AES128_GCM:
			vcp->vc3_enc_cipherid = cipher;
			break;
		case SMB3_CIPHER_AES256_CCM:
		case SMB3_CIPHER_AES128_CCM:
			vcp->vc3_enc_cipherid = cipher;
			break;
		default:
			err = EINVAL;
			break;
		}
	}

errout:
	return (err);
}
