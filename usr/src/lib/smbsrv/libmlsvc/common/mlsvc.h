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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2020 Tintri by DDN, Inc. All rights reserved.
 * Copyright 2026 RackTop Systems, Inc.
 */

#ifndef _SMBSRV_MLSVC_H
#define	_SMBSRV_MLSVC_H

#include <smbsrv/smb_share.h>
#include <smbsrv/ndl/netlogon.ndl>

#ifdef __cplusplus
extern "C" {
#endif

struct netr_info;

/*
 * These flags control various parts of NetLogon RPC messages.
 * The default is 0 - setting a bit disables some feature.
 * They are set in smbd/netlogon_flags in svc:/network/smb/server.
 * These are set when smbd starts. Changing them requires
 * restarting smbd.
 *
 * These shouldn't be confused with either SamLogonEx's ExtraFlags,
 * or NetrServerAuthenticate's negotiate_flags.
 *
 * DISABLE_SECURE_RPC causes Netlogon to use unauthenticated RPC.
 * Note that the underlying transport is still authenticated and signed.
 *
 * DISABLE_RESP_VERIF instructs RPC authentication to ignore failures
 * when verifying responses.
 *
 * DISABLE_SAMLOGONEX causes Netlogon to always use SamLogon, which
 * makes use of Netlogon Authenticators.
 *
 * DISABLE_SEALING causes Netlogon to use Signing (integrity) instead of
 * Sealing (privacy).
 *
 * DISABLE_AES_AUTH causes Netlogon to use legacy RC4/MD5 instead of
 * AES/SHA256.
 *
 * These flags are intentionally private to libmlsvc and its tests;
 * they are not part of any public interface.
 */
#define	NETR_CFG_DISABLE_SECURE_RPC	0x00000001
#define	NETR_CFG_DISABLE_RESP_VERIF	0x00000002
#define	NETR_CFG_DISABLE_SAMLOGONEX	0x00000004
#define	NETR_CFG_DISABLE_SEALING	0x00000008
#define	NETR_CFG_DISABLE_AES_AUTH	0x00000010

int smb_dclocator_init(void);
void smbrdr_initialize(void);
void dssetup_initialize(void);
void srvsvc_initialize(void);
void wkssvc_initialize(void);
void lsarpc_initialize(void);
void logr_initialize(void);
void netr_initialize(void);
void samr_initialize(void);
void svcctl_initialize(void);
void winreg_initialize(void);
void msgsvcsend_initialize(void);
void spoolss_initialize(void);
void netdfs_initialize(void);

void logr_finalize(void);
void svcctl_finalize(void);
void spoolss_finalize(void);
void netdfs_finalize(void);

/* netr_auth.c */
/* No RPC-level auth */
DWORD netr_open(char *, char *, mlsvc_handle_t *);
/* Uses RPC-level auth if supported */
DWORD netr_open_secure(char *, char *, mlsvc_handle_t *);
int netr_close(mlsvc_handle_t *);
DWORD netlogon_auth(char *, char *, DWORD);
int netr_setup_authenticator(struct netr_info *, struct netr_authenticator *,
    struct netr_authenticator *);
DWORD netr_validate_chain(struct netr_info *, struct netr_authenticator *);

uint32_t smb_netlogon_check(char *, char *);

int srvsvc_gettime(unsigned long *);
void srvsvc_timecheck(char *, char *);

/* Generic functions to get/set windows Security Descriptors */
uint32_t srvsvc_sd_get(smb_share_t *, uint8_t *, uint32_t *);
uint32_t srvsvc_sd_set(smb_share_t *, uint8_t *);

uint32_t smb_logon_init(void);
void smb_logon_fini(void);

/* Locking for process-wide settings (i.e. privileges) */
void smb_proc_initsem(void);	/* init (or re-init in child) */
int  smb_proc_takesem(void);	/* parent before */
void smb_proc_givesem(void);	/* parent after */

/* Quota */
void smb_quota_init(void);
void smb_quota_fini(void);
void smb_quota_add_fs(const char *);
void smb_quota_remove_fs(const char *);

uint32_t smb_ddiscover_main(char *, smb_domainex_t *);

#ifdef __cplusplus
}
#endif
#endif /* _SMBSRV_MLSVC_H */
