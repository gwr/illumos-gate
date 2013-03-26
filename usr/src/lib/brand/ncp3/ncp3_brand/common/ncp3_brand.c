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

#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <thread.h>
#include <sys/auxv.h>
#include <sys/brand.h>
#include <sys/inttypes.h>
#include <sys/lwp.h>
#include <sys/syscall.h>
#include <sys/systm.h>
#include <sys/utsname.h>
#include <sys/sysconfig.h>
#include <sys/systeminfo.h>
#include <sys/zone.h>
#include <sys/stat.h>
#include <sys/mntent.h>
#include <sys/ctfs.h>
#include <sys/priv.h>
#include <sys/acctctl.h>
#include <libgen.h>
#include <bsm/audit.h>
#include <sys/crypto/ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/ucontext.h>
#include <sys/mntio.h>
#include <sys/mnttab.h>
#include <sys/attr.h>
#include <sys/lofi.h>
#include <atomic.h>
#include <sys/acl.h>

#include <ncp3_brand.h>
#include <brand_misc.h>
#include <ncp3_misc.h>

/*
 * See usr/src/lib/brand/shared/brand/common/brand_util.c for general
 * emulation notes.
 */

static zoneid_t zoneid;
static boolean_t emul_global_zone = B_FALSE;
pid_t zone_init_pid;

brand_sysent_table_t brand_sysent_table[];

#define	NCP3_UTS_RELEASE	"5.11"
#define	NCP3_UTS_VERSION	"NexentaOS_134f"

/*
 * If the ioctl fd's major doesn't match "major", then pass through the
 * ioctl, since it is not the expected device.  major should be a
 * pointer to a static dev_t initialized to -1, and devname should be
 * the path of the device.
 *
 * Returns 1 if the ioctl was handled (in which case *err contains the
 * error code), or 0 if it still needs handling.
 */
static int
passthru_otherdev_ioctl(dev_t *majordev, const char *devname, int *err,
    sysret_t *rval, int fdes, int cmd, intptr_t arg)
{
	struct stat sbuf;

	if (*majordev == (dev_t)-1) {
		if ((*err = __systemcall(rval, SYS_fstatat + 1024,
		    AT_FDCWD, devname, &sbuf, 0) != 0) != 0)
			goto doioctl;

		*majordev = major(sbuf.st_rdev);
	}

	if ((*err = __systemcall(rval, SYS_fstatat + 1024, fdes,
	    NULL, &sbuf, 0)) != 0)
		goto doioctl;

	if (major(sbuf.st_rdev) == *majordev)
		return (0);

doioctl:
	*err = (__systemcall(rval, SYS_ioctl + 1024, fdes, cmd, arg));
	return (1);
}

/*
 * Figures out the PID of init for the zone.  Also returns a boolean
 * indicating whether this process currently has that pid: if so,
 * then at this moment, we are init.
 */
static boolean_t
get_initpid_info(void)
{
	pid_t pid;
	sysret_t rval;
	int err;

	/*
	 * Determine the current process PID and the PID of the zone's init.
	 * We use care not to call getpid() here, because we're not supposed
	 * to call getpid() until after the program is fully linked-- the
	 * first call to getpid() is a signal from the linker to debuggers
	 * that linking has been completed.
	 */
	if ((err = __systemcall(&rval, SYS_brand,
	    B_NCP3_PIDINFO, &pid, &zone_init_pid)) != 0) {
		brand_abort(err, "Failed to get init's pid");
	}

	/*
	 * Note that we need to be cautious with the pid we get back--
	 * it should not be stashed and used in place of getpid(), since
	 * we might fork(2).  So we keep zone_init_pid and toss the pid
	 * we otherwise got.
	 */
	if (pid == zone_init_pid)
		return (B_TRUE);

	return (B_FALSE);
}


/*
 * Assign the structure member value from the s (source) structure to the
 * d (dest) structure.
 */
#define	struct_assign(d, s, val)	(((d).val) = ((s).val))



/*
 * ZFS ioctls have changed many times in OpenSolaris releases, and since
 * the NCP base. The brand wraps ZFS commands so that the native commands
 * are used, but we want to be sure no command sneaks in that uses ZFS
 * without our knowledge.  We'll abort the process if we see a ZFS ioctl.
 */
static int
zfs_ioctl(sysret_t *rval, int fdes, int cmd, intptr_t arg)
{
	static dev_t zfs_dev = (dev_t)-1;
	int err;

	if (passthru_otherdev_ioctl(&zfs_dev, ZFS_DEV, &err,
	    rval, fdes, cmd, arg) == 1)
		return (err);

	brand_abort(0, "ZFS ioctl!");
	/*NOTREACHED*/
	return (0);
}


int
ncp3_ioctl(sysret_t *rval, int fdes, int cmd, intptr_t arg)
{

	switch (cmd & ~0xff) {
	case ZFS_IOC:
		return (zfs_ioctl(rval, fdes, cmd, arg));

	default:
		break;
	}

	return (__systemcall(rval, SYS_ioctl + 1024, fdes, cmd, arg));
}


/*
 * The definition of a trivial ace-style ACL (used by ZFS and NFSv4) has been
 * simplified since onnv134, but the common ACL code was backported, so
 * no emulation of acl, facl is needed.
 */


/*
 * Determine whether the executable passed to SYS_exec or SYS_execve is a
 * native executable.  The ncp3_npreload.so invokes the B_NCP3_NATIVE brand
 * operation which patches up the processes exec info to eliminate any trace
 * of the wrapper.  That will make pgrep and other commands that examine
 * process' executable names and command-line parameters work properly.
 */
static int
ncp3_exec_native(sysret_t *rval, const char *fname, const char **argp,
    const char **envp)
{
	const char *filename = fname;
	char path[64];
	int err;

	/* Get a copy of the executable we're trying to run */
	path[0] = '\0';
	(void) brand_uucopystr(filename, path, sizeof (path));

	/* Check if we're trying to run a native binary */
	if (strncmp(path, "/.SUNWnative/usr/lib/brand/ncp3/ncp3_native",
	    sizeof (path)) != 0)
		return (0);

	/* Skip the first element in the argv array */
	argp++;

	/*
	 * The the path of the dynamic linker is the second parameter
	 * of ncp3_native_exec().
	 */
	if (brand_uucopy(argp, &filename, sizeof (char *)) != 0)
		return (EFAULT);

	/* If an exec call succeeds, it never returns */
	err = __systemcall(rval, SYS_brand + 1024, B_EXEC_NATIVE, filename,
	    argp, envp, NULL, NULL, NULL);
	brand_assert(err != 0);
	return (err);
}

/*
 * Interpose on the SYS_exec syscall to detect native wrappers.
 */
int
ncp3_exec(sysret_t *rval, const char *fname, const char **argp)
{
	int err;

	if ((err = ncp3_exec_native(rval, fname, argp, NULL)) != 0)
		return (err);

	/* If an exec call succeeds, it never returns */
	err = __systemcall(rval, SYS_execve + 1024, fname, argp, NULL);
	brand_assert(err != 0);
	return (err);
}

/*
 * Interpose on the SYS_execve syscall to detect native wrappers.
 */
int
ncp3_execve(sysret_t *rval, const char *fname, const char **argp,
    const char **envp)
{
	int err;

	if ((err = ncp3_exec_native(rval, fname, argp, envp)) != 0)
		return (err);

	/* If an exec call succeeds, it never returns */
	err = __systemcall(rval, SYS_execve + 1024, fname, argp, envp);
	brand_assert(err != 0);
	return (err);
}

static long
ncp3_uname(sysret_t *rv, uintptr_t p1)
{
	struct utsname un, *unp = (struct utsname *)p1;
	int rev, err;

	if ((err = __systemcall(rv, SYS_uname + 1024, &un)) != 0)
		return (err);

	rev = atoi(&un.release[2]);
	brand_assert(rev >= 11);
	bzero(un.release, _SYS_NMLN);
	(void) strlcpy(un.release, NCP3_UTS_RELEASE, _SYS_NMLN);
	bzero(un.version, _SYS_NMLN);
	(void) strlcpy(un.version, NCP3_UTS_VERSION, _SYS_NMLN);

	/* copy out the modified uname info */
	return (brand_uucopy(&un, unp, sizeof (un)));
}

int
ncp3_sysconfig(sysret_t *rv, int which)
{
	long value;

	/*
	 * We must interpose on the sysconfig(2) requests
	 * that deal with the realtime signal number range.
	 * All others get passed to the native sysconfig(2).
	 */
	switch (which) {
	case _CONFIG_RTSIG_MAX:
		value = NCP3_SIGRTMAX - NCP3_SIGRTMIN + 1;
		break;
	case _CONFIG_SIGRT_MIN:
		value = NCP3_SIGRTMIN;
		break;
	case _CONFIG_SIGRT_MAX:
		value = NCP3_SIGRTMAX;
		break;
	default:
		return (__systemcall(rv, SYS_sysconfig + 1024, which));
	}

	(void) B_TRUSS_POINT_1(rv, SYS_sysconfig, 0, which);
	rv->sys_rval1 = value;
	rv->sys_rval2 = 0;

	return (0);
}

int
ncp3_sysinfo(sysret_t *rv, int command, char *buf, long count)
{
	char *value;
	int len;

	/*
	 * We must interpose on the sysinfo(2) commands SI_RELEASE and
	 * SI_VERSION; all others get passed to the native sysinfo(2)
	 * command.
	 */
	switch (command) {
		case SI_RELEASE:
			value = NCP3_UTS_RELEASE;
			break;

		case SI_VERSION:
			value = NCP3_UTS_VERSION;
			break;

		default:
			/*
			 * The default action is to pass the command to the
			 * native sysinfo(2) syscall.
			 */
			return (__systemcall(rv, SYS_systeminfo + 1024,
			    command, buf, count));
	}

	len = strlen(value) + 1;
	if (count > 0) {
		if (brand_uucopystr(value, buf, count) != 0)
			return (EFAULT);

		/*
		 * Assure NULL termination of buf as brand_uucopystr() doesn't.
		 */
		if (len > count && brand_uucopy("\0", buf + (count - 1), 1)
		    != 0)
			return (EFAULT);
	}

	/*
	 * On success, sysinfo(2) returns the size of buffer required to hold
	 * the complete value plus its terminating NULL byte.
	 */
	(void) B_TRUSS_POINT_3(rv, SYS_systeminfo, 0, command, buf, count);
	rv->sys_rval1 = len;
	rv->sys_rval2 = 0;
	return (0);
}



/*
 * If the emul_global_zone flag is set then emulate some aspects of the
 * zone system call.  In particular, emulate the global zone ID on the
 * ZONE_LOOKUP subcommand and emulate some of the global zone attributes
 * on the ZONE_GETATTR subcommand.  If the flag is not set or we're performing
 * some other operation, simply pass the calls through.
 */
int
ncp3_zone(sysret_t *rval, int cmd, void *arg1, void *arg2, void *arg3,
    void *arg4)
{
	char		*aval;
	int		len;
	zoneid_t	zid;
	int		attr;
	char		*buf;
	size_t		bufsize;

	/*
	 * We only emulate the zone syscall for a subset of specific commands,
	 * otherwise we just pass the call through.
	 */
	if (!emul_global_zone)
		return (__systemcall(rval, SYS_zone + 1024, cmd, arg1, arg2,
		    arg3, arg4));

	switch (cmd) {
	case ZONE_LOOKUP:
		(void) B_TRUSS_POINT_1(rval, SYS_zone, 0, cmd);
		rval->sys_rval1 = GLOBAL_ZONEID;
		rval->sys_rval2 = 0;
		return (0);

	case ZONE_GETATTR:
		zid = (zoneid_t)(uintptr_t)arg1;
		attr = (int)(uintptr_t)arg2;
		buf = (char *)arg3;
		bufsize = (size_t)arg4;

		/*
		 * If the request is for the global zone then we're emulating
		 * that, otherwise pass this thru.
		 */
		if (zid != GLOBAL_ZONEID)
			goto passthru;

		switch (attr) {
		case ZONE_ATTR_NAME:
			aval = GLOBAL_ZONENAME;
			break;

		case ZONE_ATTR_BRAND:
			aval = NATIVE_BRAND_NAME;
			break;
		default:
			/*
			 * We only emulate a subset of the attrs, use the
			 * real zone id to pass thru the rest.
			 */
			arg1 = (void *)(uintptr_t)zoneid;
			goto passthru;
		}

		(void) B_TRUSS_POINT_5(rval, SYS_zone, 0, cmd, zid, attr,
		    buf, bufsize);

		len = strlen(aval) + 1;
		if (len > bufsize)
			return (ENAMETOOLONG);

		if (buf != NULL) {
			if (len == 1) {
				if (brand_uucopy("\0", buf, 1) != 0)
					return (EFAULT);
			} else {
				if (brand_uucopystr(aval, buf, len) != 0)
					return (EFAULT);

				/*
				 * Assure NULL termination of "buf" as
				 * brand_uucopystr() does NOT.
				 */
				if (brand_uucopy("\0", buf + (len - 1), 1) != 0)
					return (EFAULT);
			}
		}

		rval->sys_rval1 = len;
		rval->sys_rval2 = 0;
		return (0);

	default:
		break;
	}

passthru:
	return (__systemcall(rval, SYS_zone + 1024, cmd, arg1, arg2, arg3,
	    arg4));
}

/*ARGSUSED*/
int
brand_init(int argc, char *argv[], char *envp[])
{
	sysret_t		rval;
	ulong_t			ldentry;
	int			err;
	char			*bname;

	brand_pre_init();

	/*
	 * Cache the pid of the zone's init process and determine if
	 * we're init(1m) for the zone.  Remember: we might be init
	 * now, but as soon as we fork(2) we won't be.
	 */
	(void) get_initpid_info();

	/* get the current zoneid */
	err = __systemcall(&rval, SYS_zone, ZONE_LOOKUP, NULL);
	brand_assert(err == 0);
	zoneid = (zoneid_t)rval.sys_rval1;

#if 0
	/* Get the zone's emulation bitmap. */
	if ((err = __systemcall(&rval, SYS_zone, ZONE_GETATTR, zoneid,
	    NCP3_EMUL_BITMAP, emul_bitmap, sizeof (emul_bitmap))) != 0) {
		brand_abort(err, "The zone's patch level is unsupported");
		/*NOTREACHED*/
	}
#endif

	bname = basename(argv[0]);

	/*
	 * In general we want the NCP3 commands that are zone-aware to continue
	 * to behave as they normally do within a zone.  Since these commands
	 * are zone-aware, they should continue to "do the right thing".
	 * However, some zone-aware commands aren't going to work the way
	 * we expect them to inside the branded zone.  In particular, the pkg
	 * and patch commands will not properly manage all pkgs/patches
	 * unless the commands think they are running in the global zone.  For
	 * these commands we want to emulate the global zone.
	 *
	 * We don't do any emulation for pkgcond since it is typically used
	 * in pkg/patch postinstall scripts and we want those scripts to do
	 * the right thing inside a zone.
	 *
	 * One issue is the handling of hollow pkgs.  Since the pkgs are
	 * hollow, they won't use pkgcond in their postinstall scripts.  These
	 * pkgs typically are installing drivers so we handle that by
	 * replacing add_drv and rem_drv in the ncp3_boot script.
	 */
	if (strcmp("pkgadd", bname) == 0 || strcmp("pkgrm", bname) == 0 ||
	    strcmp("patchadd", bname) == 0 || strcmp("patchrm", bname) == 0)
		emul_global_zone = B_TRUE;

	ldentry = brand_post_init(NCP3_VERSION, argc, argv, envp);

	brand_runexe(argv, ldentry);
	/*NOTREACHED*/
	brand_abort(0, "brand_runexe() returned");
	return (-1);
}

/*
 * This table must have at least NSYSCALL entries in it.
 *
 * The second parameter of each entry in the brand_sysent_table
 * contains the number of parameters and flags that describe the
 * syscall return value encoding.  See the block comments at the
 * top of this file for more information about the syscall return
 * value flags and when they should be used.
 */
brand_sysent_table_t brand_sysent_table[] = {
#if defined(__sparc) && !defined(__sparcv9)
	EMULATE(brand_indir, 9 | RV_64RVAL),	/*  0 */
#else
	NOSYS,					/*  0 */
#endif
	NOSYS,					/*   1 */
	EMULATE(ncp3_forkall, 0 | RV_32RVAL2),	/*   2 */
	NOSYS,					/*   3 */
	NOSYS,					/*   4 */
	NOSYS,					/*   5 */
	NOSYS,					/*   6 */
	EMULATE(ncp3_wait, 0 | RV_32RVAL2),	/*   7 */
	EMULATE(ncp3_creat, 2 | RV_DEFAULT),	/*   8 */
	NOSYS,					/*   9 */
	NOSYS,					/*  10 */
	EMULATE(ncp3_exec, 2 | RV_DEFAULT),	/*  11 */
	NOSYS,					/*  12 */
	NOSYS,					/*  13 */
	NOSYS,					/*  14 */
	NOSYS,					/*  15 */
	NOSYS,					/*  16 */
	NOSYS,					/*  17 */
	NOSYS,					/*  18 */
	NOSYS,					/*  19 */
	NOSYS,					/*  20 */
	NOSYS,					/*  21 */
	EMULATE(ncp3_umount, 1 | RV_DEFAULT),	/*  22 */
	NOSYS,					/*  23 */
	NOSYS,					/*  24 */
	NOSYS,					/*  25 */
	NOSYS,					/*  26 */
	NOSYS,					/*  27 */
	NOSYS,					/*  28 */
	NOSYS,					/*  29 */
	EMULATE(ncp3_utime, 2 | RV_DEFAULT),	/*  30 */
	NOSYS,					/*  31 */
	NOSYS,					/*  32 */
	NOSYS,					/*  33 */
	NOSYS,					/*  34 */
	NOSYS,					/*  35 */
	NOSYS,					/*  36 */
	EMULATE(ncp3_kill, 2 | RV_DEFAULT),	/*  37 */
	NOSYS,					/*  38 */
	NOSYS,					/*  39 */
	NOSYS,					/*  40 */
	EMULATE(ncp3_dup, 1 | RV_DEFAULT),	/*  41 */
	NOSYS,					/*  42 */
	NOSYS,					/*  43 */
	NOSYS,					/*  44 */
	NOSYS,					/*  45 */
	NOSYS,					/*  46 */
	NOSYS,					/*  47 */
	NOSYS,					/*  48 */
	NOSYS,					/*  49 */
	NOSYS,					/*  50 */
	NOSYS,					/*  51 */
	NOSYS,					/*  52 */
	NOSYS,					/*  53 */
	EMULATE(ncp3_ioctl, 3 | RV_DEFAULT),	/*  54 */
	NOSYS,					/*  55 */
	NOSYS,					/*  56 */
	NOSYS,					/*  57 */
	NOSYS,					/*  58 */
	EMULATE(ncp3_execve, 3 | RV_DEFAULT),	/*  59 */
	NOSYS,					/*  60 */
	NOSYS,					/*  61 */
	NOSYS,					/*  62 */
	NOSYS,					/*  63 */
	NOSYS,					/*  64 */
	NOSYS,					/*  65 */
	NOSYS,					/*  66 */
	NOSYS,					/*  67 */
	NOSYS,					/*  68 */
	NOSYS,					/*  69 */
	NOSYS,					/*  70 */
	NOSYS,					/*  71 */
	NOSYS,					/*  72 */
	NOSYS,					/*  73 */
	NOSYS,					/*  74 */
	NOSYS,					/*  75 */
	EMULATE(ncp3_fsat, 6 | RV_DEFAULT),	/*  76 */
	NOSYS,					/*  77 */
	NOSYS,					/*  78 */
	NOSYS,					/*  79 */
	NOSYS,					/*  80 */
	NOSYS,					/*  81 */
	NOSYS,					/*  82 */
	NOSYS,					/*  83 */
	NOSYS,					/*  84 */
	NOSYS,					/*  85 */
	NOSYS,					/*  86 */
	EMULATE(ncp3_poll, 3 | RV_DEFAULT),	/*  87 */
	NOSYS,					/*  88 */
	NOSYS,					/*  89 */
	NOSYS,					/*  90 */
	NOSYS,					/*  91 */
	NOSYS,					/*  92 */
	NOSYS,					/*  93 */
	NOSYS,					/*  94 */
	EMULATE(ncp3_sigprocmask, 3 | RV_DEFAULT), /*  95 */
	NOSYS,					/*  96 */
	NOSYS,					/*  97 */
	EMULATE(ncp3_sigaction, 3 | RV_DEFAULT), /*  98 */
	EMULATE(ncp3_sigpending, 2 | RV_DEFAULT), /*  99 */
	NOSYS,					/* 100 */
	NOSYS,					/* 101 */
	NOSYS,					/* 102 */
	NOSYS,					/* 103 */
	NOSYS,					/* 104 */
	NOSYS,					/* 105 */
	NOSYS,					/* 106 */
	EMULATE(ncp3_waitid, 4 | RV_DEFAULT),	/* 107 */
	EMULATE(ncp3_sigsendsys, 2 | RV_DEFAULT), /* 108 */
	NOSYS,					/* 109 */
	NOSYS,					/* 110 */
	NOSYS,					/* 111 */
	NOSYS,					/* 112 */
	NOSYS,					/* 113 */
	NOSYS,					/* 114 */
	NOSYS,					/* 115 */
	NOSYS,					/* 116 */
	NOSYS,					/* 117 */
	NOSYS,					/* 118 */
	NOSYS,					/* 119 */
	NOSYS,					/* 120 */
	NOSYS,					/* 121 */
	NOSYS,					/* 122 */
#if defined(__x86)
	EMULATE(ncp3_xstat, 3 | RV_DEFAULT),	/* 123 */
	EMULATE(ncp3_lxstat, 3 | RV_DEFAULT),	/* 124 */
	EMULATE(ncp3_fxstat, 3 | RV_DEFAULT),	/* 125 */
	EMULATE(ncp3_xmknod, 4 | RV_DEFAULT),	/* 126 */
#else
	NOSYS,					/* 123 */
	NOSYS,					/* 124 */
	NOSYS,					/* 125 */
	NOSYS,					/* 126 */
#endif
	NOSYS,					/* 127 */
	NOSYS,					/* 128 */
	NOSYS,					/* 129 */
	NOSYS,					/* 130 */
	NOSYS,					/* 131 */
	NOSYS,					/* 132 */
	NOSYS,					/* 133 */
	NOSYS,					/* 134 */
	EMULATE(ncp3_uname, 1 | RV_DEFAULT),	/* 135 */
	NOSYS,					/* 136 */
	EMULATE(ncp3_sysconfig, 1 | RV_DEFAULT),	/* 137 */
	NOSYS,					/* 138 */
	EMULATE(ncp3_sysinfo, 3 | RV_DEFAULT),	/* 139 */
	NOSYS,					/* 140 */
	NOSYS,					/* 141 */
	NOSYS,					/* 142 */
	EMULATE(ncp3_fork1, 0 | RV_32RVAL2),	/* 143 */
	NOSYS,					/* 144 */
	NOSYS,					/* 145 */
	NOSYS,					/* 146 */
	EMULATE(ncp3_lwp_sema_wait, 1 | RV_DEFAULT), /* 147 */
	NOSYS,					/* 148 */
	NOSYS,					/* 149 */
	NOSYS,					/* 150 */
	NOSYS,					/* 151 */
	NOSYS,					/* 152 */
	NOSYS,					/* 153 */
	EMULATE(ncp3_utimes, 2 | RV_DEFAULT),	/* 154 */
	NOSYS,					/* 155 */
	NOSYS,					/* 156 */
	NOSYS,					/* 157 */
	NOSYS,					/* 158 */
	NOSYS,					/* 159 */
	NOSYS,					/* 160 */
	NOSYS,					/* 161 */
	NOSYS,					/* 162 */
	EMULATE(ncp3_lwp_kill, 2 | RV_DEFAULT),	/* 163 */
	NOSYS,					/* 164 */
	EMULATE(ncp3_lwp_sigmask, 3 | RV_32RVAL2), /* 165 */
	NOSYS,					/* 166 */
	NOSYS,					/* 167 */
	NOSYS,					/* 168 */
	EMULATE(ncp3_lwp_mutex_lock, 1 | RV_DEFAULT), /* 169 */
	NOSYS,					/* 170 */
	NOSYS,					/* 171 */
	NOSYS,					/* 172 */
	NOSYS,					/* 173 */
	NOSYS,					/* 174 */
	NOSYS,					/* 175 */
	NOSYS,					/* 176 */
	NOSYS,					/* 177 */
	NOSYS,					/* 178 */
	NOSYS,					/* 179 */
	NOSYS,					/* 180 */
	NOSYS,					/* 181 */
	NOSYS,					/* 182 */
	NOSYS,					/* 183 */
	NOSYS,					/* 184 */
	NOSYS,					/* 185 */
	NOSYS,					/* 186 */
	NOSYS,					/* 187 */
	NOSYS,					/* 188 */
	NOSYS,					/* 189 */
	EMULATE(ncp3_sigqueue, 4 | RV_DEFAULT),	/* 190 */
	NOSYS,					/* 191 */
	NOSYS,					/* 192 */
	NOSYS,					/* 193 */
	NOSYS,					/* 194 */
	NOSYS,					/* 195 */
	NOSYS,					/* 196 */
	NOSYS,					/* 197 */
	NOSYS,					/* 198 */
	NOSYS,					/* 199 */
	NOSYS,					/* 200 */
	NOSYS,					/* 201 */
	NOSYS,					/* 202 */
	NOSYS,					/* 203 */
	NOSYS,					/* 204 */
	NOSYS,					/* 205 */
	NOSYS,					/* 206 */
	NOSYS,					/* 207 */
	NOSYS,					/* 208 */
	NOSYS,					/* 209 */
	NOSYS,					/* 210 */
	NOSYS,					/* 211 */
	NOSYS,					/* 212 */
	NOSYS,					/* 213 */
	NOSYS,					/* 214 */
	NOSYS,					/* 215 */
	NOSYS,					/* 216 */
	NOSYS,					/* 217 */
	NOSYS,					/* 218 */
	NOSYS,					/* 219 */
	NOSYS,					/* 220 */
	NOSYS,					/* 221 */
	NOSYS,					/* 222 */
	NOSYS,					/* 223 */
#if defined(_LP64)
	NOSYS,					/* 224 */
#else
	EMULATE(ncp3_creat64, 2 | RV_DEFAULT),	/* 224 */
#endif
	NOSYS,					/* 225 */
	NOSYS,					/* 226 */
	EMULATE(ncp3_zone, 5 | RV_DEFAULT),	/* 227 */
	NOSYS,					/* 228 */
	NOSYS,					/* 229 */
	NOSYS,					/* 230 */
	NOSYS,					/* 231 */
	NOSYS,					/* 232 */
	NOSYS,					/* 233 */
	NOSYS,					/* 234 */
	NOSYS,					/* 235 */
	NOSYS,					/* 236 */
	NOSYS,					/* 237 */
	NOSYS,					/* 238 */
	NOSYS,					/* 239 */
	NOSYS,					/* 240 */
	NOSYS,					/* 241 */
	NOSYS,					/* 242 */
	NOSYS,					/* 243 */
	NOSYS,					/* 244 */
	NOSYS,					/* 245 */
	NOSYS,					/* 246 */
	NOSYS,					/* 247 */
	NOSYS,					/* 248 */
	NOSYS,					/* 249 */
	NOSYS,					/* 250 */
	NOSYS,					/* 251 */
	NOSYS,					/* 252 */
	NOSYS,					/* 253 */
	NOSYS,					/* 254 */
	NOSYS					/* 255 */
};
