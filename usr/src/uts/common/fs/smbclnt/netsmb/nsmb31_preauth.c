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

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/sunddi.h>
#include <sys/crypto/api.h>
#include <netsmb/smb_conn.h>
#include <fs/smbcrypt/smb_kcrypt.h>

#include <netsmb/mchain.h>

/*
 * (called from smb2_negotiate_common)
 */
int
smb31_preauth_init(smb_vc_t *vcp)
{
	int rc;

	rc = smb3_sha512_getmech(&vcp->vc_preauthmech);
	if (rc != 0) {
		cmn_err(CE_NOTE, "smb can't get pre-auth mechanism");
		return (EAUTH);
	}

	return (rc);
}

/* ARGSUSED */
int
smb31_preauth_calc(smb_vc_t *vcp, mblk_t *mb,
    uint8_t *in_hashval, uint8_t *out_hashval)
{
	smb_sign_ctx_t ctx = 0;
	int rc;

	if ((rc = smb3_sha512_init(&ctx, &vcp->vc_preauthmech)) != 0)
		return (rc);

	/* Digest current hashval */
	rc = smb3_sha512_update(ctx, in_hashval, SHA512_DIGEST_LENGTH);
	if (rc != 0)
		return (rc);

	while (mb != NULL) {
		int len = MBLKL(mb);

		rc = smb3_sha512_update(ctx, mb->b_rptr, len);
		if (rc != 0)
			return (rc);
		mb = mb->b_cont;
	}

	rc = smb3_sha512_final(ctx, out_hashval);

	return (rc);
}
