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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2013, OmniTI Computer Consulting, Inc. All rights reserved.
 */

#ifndef _NCP3_BRAND_H
#define	_NCP3_BRAND_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/brand.h>

#define	NCP3_BRANDNAME		"ncp3"

#define	NCP3_VERSION_1		1
#define	NCP3_VERSION		NCP3_VERSION_1

#define	NCP3_LIB_NAME		"ncp3_brand.so.1"
#define	NCP3_LINKER_NAME		"ld.so.1"

#define	NCP3_LIB32		BRAND_NATIVE_DIR "usr/lib/" NCP3_LIB_NAME
#define	NCP3_LINKER32		"/lib/" NCP3_LINKER_NAME

#define	NCP3_LIB64		BRAND_NATIVE_DIR "usr/lib/64/" NCP3_LIB_NAME
#define	NCP3_LINKER64		"/lib/64/" NCP3_LINKER_NAME

#if defined(_LP64)
#define	NCP3_LIB		NCP3_LIB64
#define	NCP3_LINKER	NCP3_LINKER64
#else /* !_LP64 */
#define	NCP3_LIB		NCP3_LIB32
#define	NCP3_LINKER	NCP3_LINKER32
#endif /* !_LP64 */

/*
 * NCP3 value of _SIGRTMIN, _SIGRTMAX, MAXSIG, NSIG
 *
 * Note that NCP3 (and onnv_134) has the same sigset_t,
 * but _SIGRTMAX is 48 where native _SIGRTMAX is 72.
 * Other than that, the signal numbers are identical.
 */
#define	NCP3_SIGRTMIN	41
#define	NCP3_SIGRTMAX	48
#define	NCP3_MAXSIG	48
#define	NCP3_NSIG	49

/*
 * Brand system call subcodes.  0-127 are reserved for generic subcodes.
 */
#define	B_NCP3_PIDINFO		128
#define	B_NCP3_NATIVE		130


/*
 * This string constant represents the path of the NCP3 directory
 * containing emulation feature files.
 */
#define	NCP3_REQ_EMULATION_DIR	"/usr/lib/brand/ncp3"

/*
 * ncp3_brand_syscall_callback_common() needs to save 4 local registers so it
 * can free them up for its own use.
 */
#define	NCP3_CPU_REG_SAVE_SIZE	(sizeof (ulong_t) * 4)

/*
 * NCP3 system call codes for NCP3 traps that have been removed or reassigned,
 * or that are to be removed or reassigned after the dtrace syscall provider
 * has been reengineered to deal properly with syscall::open (for example).
 */
#define	NCP3_SYS_forkall	2
#define	NCP3_SYS_wait		7
#define	NCP3_SYS_creat		8
#define	NCP3_SYS_exec		11
#define	NCP3_SYS_umount		22
#define	NCP3_SYS_utime		30
/* SYS_kill 37 */
#define	NCP3_SYS_dup		41
#define	NCP3_SYS_pipe		42
/* SYS_ioctl 54 */
/* SYS_execve 59 */
#define	NCP3_SYS_fsat		76
#define	NCP3_SYS_poll		87
/* SYS_sigprocmask 95 */
/* SYS_sigaction 98 */
/* SYS_sigpending 99 */
/* SYS_waidid 107 */
/* SYS_sigsendsys 108 */
#define	NCP3_SYS_xstat		123
#define	NCP3_SYS_lxstat		124
#define	NCP3_SYS_fxstat		125
#define	NCP3_SYS_xmknod		126
/* SYS_uname 135 */
/* SYS_sysconfig 137 */
/* SYS_sysinfo 139 */
#define	NCP3_SYS_fork1		143
#define	NCP3_SYS_lwp_sema_wait	147
#define	NCP3_SYS_utimes		154
/* SYS_lwp_kill 163 */
/* SYS_lwp_sigmask 165 */
#define	NCP3_SYS_lwp_mutex_lock	169
/* SYS_sigqueue 190 */
#define	NCP3_SYS_creat64	224
/* SYS_zone 227 */
#define	NCP3_SYS_so_socket	230
#define	NCP3_SYS_accept		234


/*
 * ncp3-brand-specific attributes
 * These must start at ZONE_ATTR_BRAND_ATTRS.
 */


#if defined(_KERNEL)

void ncp3_brand_syscall_callback(void);
void ncp3_brand_syscall32_callback(void);

#if !defined(sparc)
void ncp3_brand_sysenter_callback(void);
#endif /* !sparc */

#if defined(__amd64)
void ncp3_brand_int91_callback(void);
#endif /* __amd64 */
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _NCP3_BRAND_H */
