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
 * Copyright 2021 RackTop Systems, Inc.
 */

#ifndef	_SMB31_PREAUTH_H_
#define	_SMB31_PREAUTH_H_

/*
 * SMB 3.1.1 pre-authentication routines.
 */

#ifdef	_KERNEL
#include <sys/crypto/api.h>
#include <netsmb/smb_conn.h>
#include <sys/stream.h>
#else
#include <security/cryptoki.h>
#include <security/pkcs11.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

int smb31_preauth_init(smb_vc_t *);
int smb31_preauth_calc(smb_vc_t *vcp, mblk_t *mb, uint8_t *, uint8_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _SMB31_PREAUTH_H_ */
