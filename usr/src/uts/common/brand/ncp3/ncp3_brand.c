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

#include <sys/errno.h>
#include <sys/exec.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/model.h>
#include <sys/proc.h>
#include <sys/syscall.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/cmn_err.h>
#include <sys/archsystm.h>
#include <sys/pathname.h>
#include <sys/sunddi.h>

#include <sys/machbrand.h>
#include <sys/brand.h>
#include "ncp3_brand.h"

char *ncp3_emulation_table = NULL;

void	ncp3_init_brand_data(zone_t *);
void	ncp3_free_brand_data(zone_t *);
void	ncp3_setbrand(proc_t *);
int	ncp3_getattr(zone_t *, int, void *, size_t *);
int	ncp3_setattr(zone_t *, int, void *, size_t);
int	ncp3_brandsys(int, int64_t *, uintptr_t, uintptr_t, uintptr_t,
		uintptr_t, uintptr_t, uintptr_t);
void	ncp3_copy_procdata(proc_t *, proc_t *);
void	ncp3_proc_exit(struct proc *, klwp_t *);
void	ncp3_exec();
int	ncp3_initlwp(klwp_t *);
void	ncp3_forklwp(klwp_t *, klwp_t *);
void	ncp3_freelwp(klwp_t *);
void	ncp3_lwpexit(klwp_t *);
int	ncp3_elfexec(vnode_t *, execa_t *, uarg_t *, intpdata_t *, int,
	long *, int, caddr_t, cred_t *, int);
void	ncp3_sigset_native_to_ncp3(sigset_t *);
void	ncp3_sigset_ncp3_to_native(sigset_t *);

/* ncp3 brand */
struct brand_ops ncp3_brops = {
	ncp3_init_brand_data,
	ncp3_free_brand_data,
	ncp3_brandsys,
	ncp3_setbrand,
	ncp3_getattr,
	ncp3_setattr,
	ncp3_copy_procdata,
	ncp3_proc_exit,
	ncp3_exec,
	lwp_setrval,
	ncp3_initlwp,
	ncp3_forklwp,
	ncp3_freelwp,
	ncp3_lwpexit,
	ncp3_elfexec,
	ncp3_sigset_native_to_ncp3,
	ncp3_sigset_ncp3_to_native,
	NCP3_NSIG,
};

#ifdef	sparc

struct brand_mach_ops ncp3_mops = {
	ncp3_brand_syscall_callback,
	ncp3_brand_syscall32_callback
};

#else	/* sparc */

#ifdef	__amd64

struct brand_mach_ops ncp3_mops = {
	ncp3_brand_sysenter_callback,
	ncp3_brand_int91_callback,
	ncp3_brand_syscall_callback,
	ncp3_brand_syscall32_callback
};

#else	/* ! __amd64 */

struct brand_mach_ops ncp3_mops = {
	ncp3_brand_sysenter_callback,
	NULL,
	ncp3_brand_syscall_callback,
	NULL
};
#endif	/* __amd64 */

#endif	/* _sparc */

struct brand	ncp3_brand = {
	BRAND_VER_1,
	"ncp3",
	&ncp3_brops,
	&ncp3_mops
};

static struct modlbrand modlbrand = {
	&mod_brandops,		/* type of module */
	"NCP3 Brand",		/* description of module */
	&ncp3_brand		/* driver ops */
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modlbrand, NULL
};

void
ncp3_setbrand(proc_t *p)
{
	brand_solaris_setbrand(p, &ncp3_brand);
}

/*ARGSUSED*/
int
ncp3_getattr(zone_t *zone, int attr, void *buf, size_t *bufsize)
{
	return (EINVAL);
}

int
ncp3_setattr(zone_t *zone, int attr, void *buf, size_t bufsize)
{
	return (EINVAL);
}


/*
 * Native processes are started with the native ld.so.1 as the command.  This
 * brand op is invoked by ncp3_npreload to fix up the command and arguments
 * so that apps like pgrep or ps see the expected command strings.
 */
int
ncp3_native(void *cmd, void *args)
{
	struct user	*up = PTOU(curproc);
	char		cmd_buf[MAXCOMLEN + 1];
	char		arg_buf[PSARGSZ];

	if (copyin(cmd, &cmd_buf, sizeof (cmd_buf)) != 0)
		return (EFAULT);
	if (copyin(args, &arg_buf, sizeof (arg_buf)) != 0)
		return (EFAULT);

	/*
	 * Make sure that the process' interpreter is the native dynamic linker.
	 * Convention dictates that native processes executing within ncp3-
	 * branded zones are interpreted by the native dynamic linker (the
	 * process and its arguments are specified as arguments to the dynamic
	 * linker).  If this convention is violated (i.e.,
	 * brandsys(B_NCP3_NATIVE, ...) is invoked by a process that shouldn't
	 * be native), then do nothing and silently indicate success.
	 */
	if (strcmp(up->u_comm, NCP3_LINKER_NAME) != 0)
		return (0);

	/*
	 * The sizeof has an extra value for the trailing '\0' so this covers
	 * the appended " " in the following strcmps.
	 */
	if (strncmp(up->u_psargs, BRAND_NATIVE_LINKER64 " ",
	    sizeof (BRAND_NATIVE_LINKER64)) != 0 &&
	    strncmp(up->u_psargs, BRAND_NATIVE_LINKER32 " ",
	    sizeof (BRAND_NATIVE_LINKER32)) != 0)
		return (0);

	mutex_enter(&curproc->p_lock);
	(void) strlcpy(up->u_comm, cmd_buf, sizeof (up->u_comm));
	(void) strlcpy(up->u_psargs, arg_buf, sizeof (up->u_psargs));
	mutex_exit(&curproc->p_lock);

	return (0);
}

/*ARGSUSED*/
int
ncp3_brandsys(int cmd, int64_t *rval, uintptr_t arg1, uintptr_t arg2,
    uintptr_t arg3, uintptr_t arg4, uintptr_t arg5, uintptr_t arg6)
{
	proc_t	*p = curproc;
	int	res;

	*rval = 0;

	if (cmd == B_NCP3_NATIVE)
		return (ncp3_native((void *)arg1, (void *)arg2));

	res = brand_solaris_cmd(cmd, arg1, arg2, arg3, &ncp3_brand,
	    NCP3_VERSION);
	if (res >= 0)
		return (res);

	switch ((cmd)) {
	case B_NCP3_PIDINFO:
		/*
		 * The ncp3 brand needs to be able to get the pid of the
		 * current process and the pid of the zone's init, and it
		 * needs to do this on every process startup.  Early in
		 * brand startup, we can't call getpid() because calls to
		 * getpid() represent a magical signal to some old-skool
		 * debuggers.  By merging all of this into one call, we
		 * make this quite a bit cheaper and easier to handle in
		 * the brand module.
		 */
		if (copyout(&p->p_pid, (void *)arg1, sizeof (pid_t)) != 0)
			return (EFAULT);
		if (copyout(&p->p_zone->zone_proc_initpid, (void *)arg2,
		    sizeof (pid_t)) != 0)
			return (EFAULT);
		return (0);

	default:
		break;
	}

	return (EINVAL);
}

void
ncp3_copy_procdata(proc_t *child, proc_t *parent)
{
	brand_solaris_copy_procdata(child, parent, &ncp3_brand);
}

void
ncp3_proc_exit(struct proc *p, klwp_t *l)
{
	brand_solaris_proc_exit(p, l, &ncp3_brand);
}

void
ncp3_exec()
{
	brand_solaris_exec(&ncp3_brand);
}

int
ncp3_initlwp(klwp_t *l)
{
	return (brand_solaris_initlwp(l, &ncp3_brand));
}

void
ncp3_forklwp(klwp_t *p, klwp_t *c)
{
	brand_solaris_forklwp(p, c, &ncp3_brand);
}

void
ncp3_freelwp(klwp_t *l)
{
	brand_solaris_freelwp(l, &ncp3_brand);
}

void
ncp3_lwpexit(klwp_t *l)
{
	brand_solaris_lwpexit(l, &ncp3_brand);
}

void
ncp3_free_brand_data(zone_t *zone)
{
}

void
ncp3_init_brand_data(zone_t *zone)
{
}

int
ncp3_elfexec(vnode_t *vp, execa_t *uap, uarg_t *args, intpdata_t *idatap,
	int level, long *execsz, int setid, caddr_t exec_file, cred_t *cred,
	int brand_action)
{
	return (brand_solaris_elfexec(vp, uap, args, idatap, level, execsz,
	    setid, exec_file, cred, brand_action, &ncp3_brand, NCP3_BRANDNAME,
	    NCP3_LIB, NCP3_LIB32, NCP3_LINKER, NCP3_LINKER32));
}

void
ncp3_sigset_native_to_ncp3(sigset_t *set)
{
	sigset_t ncp3set;

	/*
	 * Shortcut: we know the first 48 signals are identical
	 * between ncp3 and native, so just assign and mask out
	 * any bits for signals not known in NCP3.
	 */
	ncp3set.__sigbits[0] = set->__sigbits[0];
	ncp3set.__sigbits[1] = set->__sigbits[1] & 0xFFFF;
	ncp3set.__sigbits[2] = 0;
	ncp3set.__sigbits[3] = 0;

	*set = ncp3set;
}

void
ncp3_sigset_ncp3_to_native(sigset_t *set)
{
	sigset_t nativeset;

	/*
	 * Shortcut: we know the first 48 signals are identical in
	 * ncp3 and native (and a proper subset) so just assign.
	 */
	nativeset.__sigbits[0] = set->__sigbits[0];
	nativeset.__sigbits[1] = set->__sigbits[1];
	nativeset.__sigbits[2] = 0;
	nativeset.__sigbits[3] = 0;

	*set = nativeset;
}

int
_init(void)
{
	int err;

	/*
	 * Set up the table indicating which system calls we want to
	 * interpose on.  We should probably build this automatically from
	 * a list of system calls that is shared with the user-space
	 * library.
	 */
	ncp3_emulation_table = kmem_zalloc(NSYSCALL, KM_SLEEP);
	ncp3_emulation_table[NCP3_SYS_forkall] = 1;		/*   2 */
	ncp3_emulation_table[NCP3_SYS_wait] = 1;		/*   7 */
	ncp3_emulation_table[NCP3_SYS_creat] = 1;		/*   8 */
	ncp3_emulation_table[NCP3_SYS_exec] = 1;		/*  11 */
	ncp3_emulation_table[NCP3_SYS_umount] = 1;		/*  22 */
	ncp3_emulation_table[NCP3_SYS_utime] = 1;		/*  30 */
	ncp3_emulation_table[SYS_kill] = 1;			/*  37 */
	ncp3_emulation_table[NCP3_SYS_dup] = 1;			/*  41 */
	ncp3_emulation_table[SYS_ioctl] = 1;			/*  54 */
	ncp3_emulation_table[SYS_execve] = 1;			/*  59 */
	ncp3_emulation_table[NCP3_SYS_fsat] = 1;		/*  76 */
	ncp3_emulation_table[NCP3_SYS_poll] = 1;		/*  87 */
	ncp3_emulation_table[SYS_sigprocmask] = 1;		/*  95 */
	ncp3_emulation_table[SYS_sigaction] = 1;		/*  98 */
	ncp3_emulation_table[SYS_sigpending] = 1;		/*  99 */
	ncp3_emulation_table[SYS_waitid] = 1;			/* 107 */
	ncp3_emulation_table[SYS_sigsendsys] = 1;		/* 108 */
#if defined(__x86)
	ncp3_emulation_table[NCP3_SYS_xstat] = 1;		/* 123 */
	ncp3_emulation_table[NCP3_SYS_lxstat] = 1;		/* 124 */
	ncp3_emulation_table[NCP3_SYS_fxstat] = 1;		/* 125 */
	ncp3_emulation_table[NCP3_SYS_xmknod] = 1;		/* 126 */
#endif
	ncp3_emulation_table[SYS_uname] = 1;			/* 135 */
	ncp3_emulation_table[SYS_sysconfig] = 1;		/* 137 */
	ncp3_emulation_table[SYS_systeminfo] = 1;		/* 139 */
	ncp3_emulation_table[NCP3_SYS_fork1] = 1;		/* 143 */
	ncp3_emulation_table[NCP3_SYS_lwp_sema_wait] = 1;	/* 147 */
	ncp3_emulation_table[NCP3_SYS_utimes] = 1;		/* 154 */
	ncp3_emulation_table[SYS_lwp_kill] = 1;			/* 163 */
	ncp3_emulation_table[SYS_lwp_sigmask] = 1;		/* 165 */
	ncp3_emulation_table[NCP3_SYS_lwp_mutex_lock] = 1;	/* 169 */
	ncp3_emulation_table[SYS_sigqueue] = 1;			/* 190 */
	ncp3_emulation_table[NCP3_SYS_creat64] = 1;		/* 224 */
	ncp3_emulation_table[SYS_zone] = 1;			/* 227 */

	err = mod_install(&modlinkage);
	if (err) {
		cmn_err(CE_WARN, "Couldn't install brand module");
		kmem_free(ncp3_emulation_table, NSYSCALL);
	}

	return (err);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}

int
_fini(void)
{
	return (brand_solaris_fini(&ncp3_emulation_table, &modlinkage,
	    &ncp3_brand));
}
