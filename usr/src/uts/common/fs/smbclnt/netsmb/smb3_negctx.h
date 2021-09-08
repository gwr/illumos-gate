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

#ifndef _NETSMB_SMB3_NEGCTX_H_
#define	_NETSMB_SMB3_NEGCTX_H_

#include <sys/param.h>
#include <netsmb/smb_osdep.h>
#include <netsmb/smb.h>
#include <netsmb/smb2.h>
#include <netsmb/smb_conn.h>
#include <netsmb/mchain.h>
#include <fs/smbcrypt/smb_kcrypt.h>

enum smb2_neg_ctx_type {
	SMB2_PREAUTH_INTEGRITY_CAPS		= 1,
	SMB2_ENCRYPTION_CAPS			= 2,
	SMB2_COMPRESSION_CAPS			= 3,	/* not imlemented */
	SMB2_NETNAME_NEGOTIATE_CONTEXT_ID	= 5	/* not imlemented */
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
#define	MAX_CIPHER_NUM	(2)

/*
 * SMB 3.1.1 specifies the only hashing algorithm - SHA-512 and
 * two encryption ones - AES-128-CCM and AES-128-GCM.
 */
#define	MAX_HASHID_NUM	(1)
#define	MAX_CIPHER_NUM	(2)

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

#define	NEG_CTX_INFO_OFFSET	(SMB2_HDR_SIZE + 28)
#define	NEG_CTX_OFFSET_OFFSET	(SMB2_HDR_SIZE + 64)
#define	NEG_CTX_MAX_COUNT	(16)
#define	NEG_CTX_MAX_DATALEN	(256)

#define	STATUS_SMB_NO_PREAUTH_INEGRITY_HASH_OVERLAP	(0xC05D0000)

#define	STATUS_PREAUTH_HASH_OVERLAP \
    STATUS_SMB_NO_PREAUTH_INEGRITY_HASH_OVERLAP

uint32_t smb31_decode_negctxs(struct smb_vc *, struct mdchain *,
    smb2_neg_ctxs_t *);

#endif /* _NETSMB_SMB3_NEGCTX_H_*/
