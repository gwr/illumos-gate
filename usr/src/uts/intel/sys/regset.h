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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 *
 * Copyright (c) 1989, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*	Copyright (c) 1990, 1991 UNIX System Laboratories, Inc. */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T		*/
/*	All Rights Reserved	*/

#ifndef	_SYS_REGSET_H
#define	_SYS_REGSET_H

#include <sys/feature_tests.h>

#if !defined(_ASM)
#include <sys/types.h>
#endif
#include <sys/mcontext.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The names and offsets defined here should be specified by the
 * AMD64 ABI suppl.
 *
 * See sys/mcontext.h gregset_t (amd64), gregset64_t, _NGREG64
 * The REG_* defines here are indicies in the 64-bit register arrays.
 *
 * We make fsbase and gsbase part of the lwp context (since they're
 * the only way to access the full 64-bit address range via the segment
 * registers) and thus belong here too.  However we treat them as
 * read-only; if %fs or %gs are updated, the results of the descriptor
 * table lookup that those updates implicitly cause will be reflected
 * in the corresponding fsbase and/or gsbase values the next time the
 * context can be inspected.  However it is NOT possible to override
 * the fsbase/gsbase settings via this interface.
 *
 * Direct modification of the base registers (thus overriding the
 * descriptor table base address) can be achieved with _lwp_setprivate.
 */

#define	REG_GSBASE	27
#define	REG_FSBASE	26
#define	REG_DS		25
#define	REG_ES		24

#define	REG_GS		23
#define	REG_FS		22
#define	REG_SS		21
#define	REG_RSP		20
#define	REG_RFL		19
#define	REG_CS		18
#define	REG_RIP		17
#define	REG_ERR		16
#define	REG_TRAPNO	15
#define	REG_RAX		14
#define	REG_RCX		13
#define	REG_RDX		12
#define	REG_RBX		11
#define	REG_RBP		10
#define	REG_RSI		9
#define	REG_RDI		8
#define	REG_R8		7
#define	REG_R9		6
#define	REG_R10		5
#define	REG_R11		4
#define	REG_R12		3
#define	REG_R13		2
#define	REG_R14		1
#define	REG_R15		0

/*
 * The names and offsets defined here are specified by i386 ABI suppl.
 *
 * See sys/mcontext.h gregset_t (for i386), gregset32_t, _NGREG32
 * The REG32_* defines here are indicies in the 32-bit register arrays.
 * Note that REG32_GS != REG_GS and in amd64 code we sometimes need to
 * handle both 64-bit and 32-bit register arrays. The 32-bit ones are
 * defined here with the REG32_ prefix to help distinguish the two.
 */

#define	REG32_SS	18	/* only stored on a privilege transition */
#define	REG32_UESP	17	/* only stored on a privilege transition */
#define	REG32_EFL	16
#define	REG32_CS	15
#define	REG32_EIP	14
#define	REG32_ERR	13
#define	REG32_TRAPNO	12
#define	REG32_EAX	11
#define	REG32_ECX	10
#define	REG32_EDX	9
#define	REG32_EBX	8
#define	REG32_ESP	7
#define	REG32_EBP	6
#define	REG32_ESI	5
#define	REG32_EDI	4
#define	REG32_DS	3
#define	REG32_ES	2
#define	REG32_FS	1
#define	REG32_GS	0

/*
 * Troublesome namespace pollution (beyond REG_*) so only defined
 * for _ASM or when requested.  These should just go away...
 */
#if defined(_ASM) || defined(_REGSET_SHORT_NAMES_)
#define	SS	REG32_SS 	/* only stored on a privilege transition */
#define	UESP	REG32_UESP	/* only stored on a privilege transition */
#define	EFL	REG32_EFL
#define	CS	REG32_CS
#define	EIP	REG32_EIP
#define	ERR	REG32_ERR
#define	TRAPNO	REG32_TRAPNO
#define	EAX	REG32_EAX
#define	ECX	REG32_ECX
#define	EDX	REG32_EDX
#define	EBX	REG32_EBX
#define	ESP	REG32_ESP
#define	EBP	REG32_EBP
#define	ESI	REG32_ESI
#define	EDI	REG32_EDI
#define	DS	REG32_DS
#define	ES	REG32_ES
#define	FS	REG32_FS
#define	GS	REG32_GS

#endif	// _ASM || ...

/* aliases for portability */

#if defined(__amd64)

#define	REG_PC	REG_RIP
#define	REG_FP	REG_RBP
#define	REG_SP	REG_RSP
#define	REG_PS	REG_RFL
#define	REG_R0	REG_RAX
#define	REG_R1	REG_RDX

#else	/* __i386 */

#define	REG_PC	REG32_EIP
#define	REG_FP	REG32_EBP
#define	REG_SP	REG32_UESP
#define	REG_PS	REG32_EFL
#define	REG_R0	REG32_EAX
#define	REG_R1	REG32_EDX

#endif	/* __i386 */

#define	NGREG	_NGREG

#if !defined(_ASM)

#ifdef	__i386
/*
 * (This structure definition is specified in the i386 ABI supplement)
 * It's likely we can just get rid of the struct __old_fpu or maybe
 * move it to $SRC/uts/intel/os/fpu.c which appears to be the
 * only place that uses it.  See: www.illumos.org/issues/6284
 */
typedef struct __old_fpu {
	union {
		struct __old_fpchip_state	/* fp extension state */
		{
			int	state[27];	/* 287/387 saved state */
			int	status;		/* status word saved at */
						/* exception */
		} fpchip_state;
		struct __old_fp_emul_space	/* for emulator(s) */
		{
			char	fp_emul[246];
			char	fp_epad[2];
		} fp_emul_space;
		int	f_fpregs[62];		/* union of the above */
	} fp_reg_set;
	long		f_wregs[33];		/* saved weitek state */
} __old_fpregset_t;
#endif	/* __i386 */

#if defined(__amd64)
#define	_NDEBUGREG	16
#else
#define	_NDEBUGREG	8
#endif

typedef struct dbregset {
	unsigned long	debugreg[_NDEBUGREG];
} dbregset_t;

#endif	/* _ASM */

/*
 * The version of privregs.h that is used on implementations that run on
 * processors that support the AMD64 instruction set is deliberately not
 * imported here.
 *
 * The amd64 'struct regs' definition may -not- compatible with either
 * 32-bit or 64-bit core file contents, nor with the ucontext.  As a result,
 * the 'regs' structure cannot be used portably by applications, and should
 * only be used by the kernel implementation.
 *
 * The inclusion of the i386 version of privregs.h allows for some limited
 * source compatibility with 32-bit applications who expect to use
 * 'struct regs' to match the context of a 32-bit core file, or a ucontext_t.
 *
 * Note that the ucontext_t actually describes the general register in terms
 * of the gregset_t data type, as described in this file.  Note also
 * that the core file content is defined by core(5) in terms of data types
 * defined by procfs -- see proc(5).
 */
#if defined(__i386) && \
	(!defined(_KERNEL) && !defined(_XPG4_2) || defined(__EXTENSIONS__))
#include <sys/privregs.h>
#endif	/* __i386 (!_KERNEL && !_XPG4_2 || __EXTENSIONS__) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_REGSET_H */
