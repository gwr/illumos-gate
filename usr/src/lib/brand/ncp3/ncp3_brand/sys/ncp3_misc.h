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
 */

#ifndef _NCP3_MISC_H
#define	_NCP3_MISC_H

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(_ASM)

extern pid_t zone_init_pid;

/*
 * From ncp3_deleted.c
 */
extern int ncp3_creat();
extern int ncp3_creat64();
extern int ncp3_fork1();
extern int ncp3_forkall();
extern int ncp3_dup();
extern int ncp3_poll();
extern int ncp3_lwp_mutex_lock();
extern int ncp3_lwp_sema_wait();
extern int ncp3_utime();
extern int ncp3_utimes();
extern int ncp3_xstat();
extern int ncp3_lxstat();
extern int ncp3_fxstat();
extern int ncp3_xmknod();
extern int ncp3_fsat();
extern int ncp3_umount();

/*
 * From ncp3_signal.c
 */

extern int ncp3_kill();

extern int ncp3_lwp_kill();
extern int ncp3_lwp_sigmask();

extern int ncp3_sigaction();
extern int ncp3_sigpending();
extern int ncp3_sigprocmask();
extern int ncp3_sigsendsys();
extern int ncp3_sigqueue();

extern int ncp3_wait();
extern int ncp3_waitid();

#endif	/* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _NCP3_MISC_H */
