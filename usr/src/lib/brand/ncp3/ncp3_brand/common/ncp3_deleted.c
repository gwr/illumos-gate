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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/file.h>
#include <sys/syscall.h>

#include <ncp3_brand.h>
#include <brand_misc.h>
#include <ncp3_misc.h>

/*
 * This file contains the emulation functions for all of the
 * obsolete system call traps that existed in NCP3 but
 * that have been deleted in the current version of illumos.
 */

static int
ncp3_fstatat(sysret_t *rval,
    int fd, const char *path, struct stat *sb, int flags)
{
	return (__systemcall(rval, SYS_fstatat + 1024,
	    fd, path, sb, flags));
}

#if !defined(_LP64)

static int
ncp3_fstatat64(sysret_t *rval,
    int fd, const char *path, struct stat64 *sb, int flags)
{
	return (__systemcall(rval, SYS_fstatat64 + 1024,
	    fd, path, sb, flags));
}

#endif	/* !_LP64 */

static int
ncp3_openat(sysret_t *rval, int fd, const char *path, int oflag, mode_t mode)
{
	return (__systemcall(rval, SYS_openat + 1024,
	    fd, path, oflag, mode));
}

int
ncp3_creat(sysret_t *rval, char *path, mode_t mode)
{
	return (__systemcall(rval, SYS_openat + 1024,
	    AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, mode));
}

#if !defined(_LP64)

static int
ncp3_openat64(sysret_t *rval, int fd, const char *path, int oflag, mode_t mode)
{
	return (__systemcall(rval, SYS_openat64 + 1024,
	    fd, path, oflag, mode));
}

int
ncp3_creat64(sysret_t *rval, char *path, mode_t mode)
{
	return (__systemcall(rval, SYS_openat64 + 1024,
	    AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC, mode));
}

#endif	/* !_LP64 */

int
ncp3_fork1(sysret_t *rval)
{
	return (__systemcall(rval, SYS_forksys + 1024, 0, 0));
}

int
ncp3_forkall(sysret_t *rval)
{
	return (__systemcall(rval, SYS_forksys + 1024, 1, 0));
}

int
ncp3_dup(sysret_t *rval, int fd)
{
	return (__systemcall(rval, SYS_fcntl + 1024, fd, F_DUPFD, 0));
}

int
ncp3_poll(sysret_t *rval, struct pollfd *fds, nfds_t nfd, int timeout)
{
	timespec_t ts;
	timespec_t *tsp;

	if (timeout < 0)
		tsp = NULL;
	else {
		ts.tv_sec = timeout / MILLISEC;
		ts.tv_nsec = (timeout % MILLISEC) * MICROSEC;
		tsp = &ts;
	}

	return (__systemcall(rval, SYS_pollsys + 1024,
	    fds, nfd, tsp, NULL));
}

int
ncp3_lwp_mutex_lock(sysret_t *rval, void *mp)
{
	return (__systemcall(rval, SYS_lwp_mutex_timedlock + 1024,
	    mp, NULL, 0));
}

int
ncp3_lwp_sema_wait(sysret_t *rval, void *sp)
{
	return (__systemcall(rval, SYS_lwp_sema_timedwait + 1024,
	    sp, NULL, 0));
}

static int
ncp3_fchownat(sysret_t *rval,
    int fd, const char *name, uid_t uid, gid_t gid, int flag)
{
	return (__systemcall(rval, SYS_fchownat + 1024,
	    fd, name, uid, gid, flag));
}

static int
ncp3_unlinkat(sysret_t *rval, int fd, const char *name, int flags)
{
	return (__systemcall(rval, SYS_unlinkat + 1024,
	    fd, name, flags));
}

static int
ncp3_renameat(sysret_t *rval,
    int oldfd, const char *oldname, int newfd, const char *newname)
{
	return (__systemcall(rval, SYS_renameat + 1024,
	    oldfd, oldname, newfd, newname));
}

static int
ncp3_faccessat(sysret_t *rval, int fd, const char *fname, int amode, int flag)
{
	return (__systemcall(rval, SYS_faccessat + 1024,
	    fd, fname, amode, flag));
}

int
ncp3_utime(sysret_t *rval, const char *path, const struct utimbuf *times)
{
	struct utimbuf ltimes;
	timespec_t ts[2];
	timespec_t *tsp;

	if (times == NULL) {
		tsp = NULL;
	} else {
		if (brand_uucopy(times, &ltimes, sizeof (ltimes)) != 0)
			return (EFAULT);
		ts[0].tv_sec = ltimes.actime;
		ts[0].tv_nsec = 0;
		ts[1].tv_sec = ltimes.modtime;
		ts[1].tv_nsec = 0;
		tsp = ts;
	}

	return (__systemcall(rval, SYS_utimesys + 1024, 1,
	    AT_FDCWD, path, tsp, 0));
}

int
ncp3_utimes(sysret_t *rval, const char *path, const struct timeval times[2])
{
	struct timeval ltimes[2];
	timespec_t ts[2];
	timespec_t *tsp;

	if (times == NULL) {
		tsp = NULL;
	} else {
		if (brand_uucopy(times, ltimes, sizeof (ltimes)) != 0)
			return (EFAULT);
		ts[0].tv_sec = ltimes[0].tv_sec;
		ts[0].tv_nsec = ltimes[0].tv_usec * 1000;
		ts[1].tv_sec = ltimes[1].tv_sec;
		ts[1].tv_nsec = ltimes[1].tv_usec * 1000;
		tsp = ts;
	}

	return (__systemcall(rval, SYS_utimesys + 1024, 1,
	    AT_FDCWD, path, tsp, 0));
}

static int
ncp3_futimesat(sysret_t *rval,
    int fd, const char *path, const struct timeval times[2])
{
	struct timeval ltimes[2];
	timespec_t ts[2];
	timespec_t *tsp;

	if (times == NULL) {
		tsp = NULL;
	} else {
		if (brand_uucopy(times, ltimes, sizeof (ltimes)) != 0)
			return (EFAULT);
		ts[0].tv_sec = ltimes[0].tv_sec;
		ts[0].tv_nsec = ltimes[0].tv_usec * 1000;
		ts[1].tv_sec = ltimes[1].tv_sec;
		ts[1].tv_nsec = ltimes[1].tv_usec * 1000;
		tsp = ts;
	}

	if (path == NULL)
		return (__systemcall(rval, SYS_utimesys + 1024, 0, fd, tsp));

	return (__systemcall(rval, SYS_utimesys + 1024, 1, fd, path, tsp, 0));
}

#if defined(__x86)

/* ARGSUSED */
int
ncp3_xstat(sysret_t *rval, int version, const char *path, struct stat *statb)
{
#if defined(__amd64)
	return (EINVAL);
#else
	if (version != _STAT_VER)
		return (EINVAL);
	return (__systemcall(rval, SYS_fstatat + 1024,
	    AT_FDCWD, path, statb, 0));
#endif
}

/* ARGSUSED */
int
ncp3_lxstat(sysret_t *rval, int version, const char *path, struct stat *statb)
{
#if defined(__amd64)
	return (EINVAL);
#else
	if (version != _STAT_VER)
		return (EINVAL);
	return (__systemcall(rval, SYS_fstatat + 1024,
	    AT_FDCWD, path, statb, AT_SYMLINK_NOFOLLOW));
#endif
}

/* ARGSUSED */
int
ncp3_fxstat(sysret_t *rval, int version, int fd, struct stat *statb)
{
#if defined(__amd64)
	return (EINVAL);
#else
	if (version != _STAT_VER)
		return (EINVAL);
	return (__systemcall(rval, SYS_fstatat + 1024,
	    fd, NULL, statb, 0));
#endif
}

/* ARGSUSED */
int
ncp3_xmknod(sysret_t *rval, int version, const char *path,
    mode_t mode, dev_t dev)
{
#if defined(__amd64)
	return (EINVAL);
#else
	if (version != _MKNOD_VER)
		return (EINVAL);
	return (__systemcall(rval, SYS_mknodat + 1024,
	    AT_FDCWD, path, mode, dev));
#endif
}

#endif	/* __x86 */

/*
 * This is the fsat() system call trap in ncp3.
 * It has been removed in the current system.
 */
int
ncp3_fsat(sysret_t *rval,
    int code, uintptr_t arg1, uintptr_t arg2,
    uintptr_t arg3, uintptr_t arg4, uintptr_t arg5)
{
	switch (code) {
	case 0:		/* openat */
		return (ncp3_openat(rval, (int)arg1,
		    (const char *)arg2, (int)arg3, (mode_t)arg4));
	case 1:		/* openat64 */
#if defined(_LP64)
		return (EINVAL);
#else
		return (ncp3_openat64(rval, (int)arg1,
		    (const char *)arg2, (int)arg3, (mode_t)arg4));
#endif
	case 2:		/* fstatat64 */
#if defined(_LP64)
		return (EINVAL);
#else
		return (ncp3_fstatat64(rval, (int)arg1,
		    (const char *)arg2, (struct stat64 *)arg3, (int)arg4));
#endif
	case 3:		/* fstatat */
		return (ncp3_fstatat(rval, (int)arg1,
		    (const char *)arg2, (struct stat *)arg3, (int)arg4));
	case 4:		/* fchownat */
		return (ncp3_fchownat(rval, (int)arg1, (char *)arg2,
		    (uid_t)arg3, (gid_t)arg4, (int)arg5));
	case 5:		/* unlinkat */
		return (ncp3_unlinkat(rval, (int)arg1, (char *)arg2,
		    (int)arg3));
	case 6:		/* futimesat */
		return (ncp3_futimesat(rval, (int)arg1,
		    (const char *)arg2, (const struct timeval *)arg3));
	case 7:		/* renameat */
		return (ncp3_renameat(rval, (int)arg1, (char *)arg2,
		    (int)arg3, (char *)arg4));
	case 8:		/* faccessat */
		return (ncp3_faccessat(rval, (int)arg1, (char *)arg2,
		    (int)arg3, (int)arg4));
	case 9:		/* openattrdirat */
		return (ncp3_openat(rval, (int)arg1,
		    (const char *)arg2, FXATTRDIROPEN, 0));
	}
	return (EINVAL);
}

/*
 * Interposition upon SYS_umount
 */
int
ncp3_umount(sysret_t *rval, const char *path)
{
	return (__systemcall(rval, SYS_umount2 + 1024, path, 0));
}
