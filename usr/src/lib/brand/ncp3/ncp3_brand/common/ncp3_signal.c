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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 */

#include <sys/types.h>
#include <sys/brand.h>
#include <sys/errno.h>
#include <sys/sysconfig.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <strings.h>
#include <signal.h>

#include <ncp3_brand.h>
#include <brand_misc.h>
#include <ncp3_misc.h>


/*
 * Theory of operation:
 *
 * NCP3 has the same sigset_t as onnv_134, which is the same size
 * as the native sigset_t.  Thanks to sigset_t compatibility, we
 * don't need most of what the old S10 brand code had here.
 *
 * NCP3 did define only 8 realtime signals (41 - 48) so this
 * simulates that in sysinfo, etc.
 */


/*
 * Convert an NCP3 signal number to its native value.
 */
static int
ncp3sig_to_native(int sig)
{
	if (sig < NCP3_NSIG)
		return (sig);

	return (-1);
}

/*
 * Convert a native sigset_t to its NCP3 version.
 */
int
nativesigset_to_ncp3(const sigset_t *native_set, sigset_t *ncp3_set)
{
	sigset_t srcset, newset;

	if (brand_uucopy(native_set, &srcset, sizeof (sigset_t)) != 0)
		return (EFAULT);

	(void) sigemptyset(&newset);

	/*
	 * Shortcut: we know the first 48 signals are identical
	 * between ncp3 and native, so just assign and mask out
	 * any bits for signals not known in NCP3.
	 */
	newset.__sigbits[0] = srcset.__sigbits[0];
	newset.__sigbits[1] = srcset.__sigbits[1] & 0xFFFF;
	newset.__sigbits[2] = 0;
	newset.__sigbits[3] = 0;

	if (brand_uucopy(&newset, ncp3_set, sizeof (sigset_t)) != 0)
		return (EFAULT);

	return (0);
}


/*
 * Interposition upon SYS_lwp_sigmask
 * Native has two more args.
 */
int
ncp3_lwp_sigmask(sysret_t *rval, int how, uint_t bits0, uint_t bits1)
{
	int err;

	err = __systemcall(rval, SYS_lwp_sigmask + 1024,
	    how, bits0, bits1, 0, 0);

	if (err == 0) {
		/* essentially nativesigset_to_ncp3() */
		/* rval->sys_rval1 is OK */
		rval->sys_rval2 &= 0xFFFF;
	}

	return (err);
}

/*
 * Interposition upon SYS_sigprocmask
 */
int
ncp3_sigprocmask(sysret_t *rval, int how, const sigset_t *set, sigset_t *oset)
{
	sigset_t sigset_oset, *oset_ptr;
	int err;

	oset_ptr = (oset == NULL) ? NULL : &sigset_oset;

	if ((err = __systemcall(rval, SYS_sigprocmask + 1024,
	    how, set, oset_ptr)) != 0)
		return (err);

	if (oset_ptr != NULL &&
	    (err = nativesigset_to_ncp3(oset_ptr, oset)) != 0)
		return (err);

	return (0);
}

/*
 * Interposition upon SYS_sigaction
 */
int
ncp3_sigaction(sysret_t *rval,
    int sig, const struct sigaction *act, struct sigaction *oact)
{
	struct sigaction osigact, *osigactp;
	int err, nativesig;

	if ((nativesig = ncp3sig_to_native(sig)) < 0) {
		(void) B_TRUSS_POINT_3(rval, SYS_sigaction, EINVAL,
		    sig, act, oact);
		return (EINVAL);
	}

	osigactp = ((oact == NULL) ? NULL : &osigact);

	if ((err = __systemcall(rval, SYS_sigaction + 1024,
	    nativesig, act, osigactp)) != 0)
		return (err);

	/*
	 * Translate the old signal mask if we are supposed to return the old
	 * struct sigaction.
	 *
	 * Note that we may have set the signal handler, but may return EFAULT
	 * here if the oact parameter is bad.
	 *
	 * That's OK, because the direct system call acts the same way.
	 */
	if (osigactp != NULL) {
		err = nativesigset_to_ncp3(&osigactp->sa_mask,
		    &osigactp->sa_mask);

		if (err == 0 && brand_uucopy(osigactp, oact,
		    sizeof (struct sigaction)) != 0)
			err = EFAULT;
	}

	return (err);
}

/*
 * Interposition upon SYS_sigpending
 *
 * Note that libc initializes static data for sigfillset
 * using: syscall(SYS_sigpending, 2, &set).  By masking
 * the set returned here, we ensure that later application
 * code using sigfillset will get only NCP3 signal bits.
 */
int
ncp3_sigpending(sysret_t *rval, int flag, sigset_t *set)
{
	sigset_t tmp_set;
	int err;

	if ((err = __systemcall(rval, SYS_sigpending + 1024,
	    flag, &tmp_set)) != 0)
		return (err);

	if ((err = nativesigset_to_ncp3(&tmp_set, set)) != 0)
		return (err);

	return (0);
}

/*
 * Interposition upon SYS_sigsendsys
 */
int
ncp3_sigsendsys(sysret_t *rval, procset_t *psp, int sig)
{
	int nativesig;

	if ((nativesig = ncp3sig_to_native(sig)) < 0) {
		(void) B_TRUSS_POINT_2(rval, SYS_sigsendsys, EINVAL,
		    psp, sig);
		return (EINVAL);
	}

	return (__systemcall(rval, SYS_sigsendsys + 1024, psp, nativesig));
}

/*
 * Convert the siginfo_t code and status fields to an old style
 * wait status for ncp3_wait(), below.
 */
static int
wstat(int code, int status)
{
	int stat = (status & 0377);

	switch (code) {
	case CLD_EXITED:
		stat <<= 8;
		break;
	case CLD_DUMPED:
		stat |= WCOREFLG;
		break;
	case CLD_KILLED:
		break;
	case CLD_TRAPPED:
	case CLD_STOPPED:
		stat <<= 8;
		stat |= WSTOPFLG;
		break;
	case CLD_CONTINUED:
		stat = WCONTFLG;
		break;
	}
	return (stat);
}

/*
 * Interposition upon SYS_wait
 */
int
ncp3_wait(sysret_t *rval)
{
	int err;
	siginfo_t info;

	err = ncp3_waitid(rval, P_ALL, 0, &info, WEXITED | WTRAPPED);
	if (err != 0)
		return (err);

	rval->sys_rval1 = info.si_pid;
	rval->sys_rval2 = wstat(info.si_code, info.si_status);

	return (0);
}

/*
 * Interposition upon SYS_waitid
 */
int
ncp3_waitid(sysret_t *rval,
    idtype_t idtype, id_t id, siginfo_t *infop, int options)
{
	int err;

	err = __systemcall(rval, SYS_waitid + 1024, idtype, id, infop, options);

	return (err);
}


/*
 * Interposition upon SYS_sigqueue
 */
int
ncp3_sigqueue(sysret_t *rval, pid_t pid, int signo, void *value, int si_code)
{
	int nativesig;

	if ((nativesig = ncp3sig_to_native(signo)) < 0) {
		(void) B_TRUSS_POINT_4(rval, SYS_sigqueue, EINVAL,
		    pid, signo, value, si_code);
		return (EINVAL);
	}

	if (pid == 1)
		pid = zone_init_pid;

	/*
	 * The native version of this syscall takes an extra argument.
	 * The new last arg "block" flag should be zero.  The block flag
	 * is used by the Opensolaris AIO implementation, which is now
	 * part of libc.
	 */
	return (__systemcall(rval, SYS_sigqueue + 1024,
	    pid, nativesig, value, si_code, 0));
}


/*
 * Interposition upon SYS_kill
 */
int
ncp3_kill(sysret_t *rval, pid_t pid, int sig)
{
	int nativesig;

	if ((nativesig = ncp3sig_to_native(sig)) < 0) {
		(void) B_TRUSS_POINT_2(rval, SYS_kill, EINVAL, pid, sig);
		return (EINVAL);
	}

	if (pid == 1)
		pid = zone_init_pid;

	return (__systemcall(rval, SYS_kill + 1024, pid, nativesig));
}


/*
 * Interposition upon SYS_lwp_kill
 */
int
ncp3_lwp_kill(sysret_t *rval, id_t lwpid, int sig)
{
	int nativesig;

	if ((nativesig = ncp3sig_to_native(sig)) < 0) {
		(void) B_TRUSS_POINT_2(rval, SYS_lwp_kill, EINVAL,
		    lwpid, sig);
		return (EINVAL);
	}

	return (__systemcall(rval, SYS_lwp_kill + 1024, lwpid, nativesig));
}
