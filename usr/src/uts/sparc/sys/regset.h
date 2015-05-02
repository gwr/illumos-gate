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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	All Rights Reserved	*/


/*
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 *
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_REGSET_H
#define	_SYS_REGSET_H

#include <sys/feature_tests.h>

#if !defined(_ASM)
#include <sys/int_types.h>
#endif
#include <sys/mcontext.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Location of the users' stored registers relative to R0.
 * Usage is as an index into a gregset_t array or as u.u_ar0[XX].
 */
#if defined(__sparcv9)
#define	REG_CCR (0)
#if defined(_SYSCALL32)
#define	REG_PSR (0)
#endif /* _SYSCALL32 */
#else
#define	REG_PSR (0)
#endif  /* __sparcv9 */

#define	REG_PC	(1)
#define	REG_nPC	(2)
#define	REG_Y	(3)
#define	REG_G1	(4)
#define	REG_G2	(5)
#define	REG_G3	(6)
#define	REG_G4	(7)
#define	REG_G5	(8)
#define	REG_G6	(9)
#define	REG_G7	(10)
#define	REG_O0	(11)
#define	REG_O1	(12)
#define	REG_O2	(13)
#define	REG_O3	(14)
#define	REG_O4	(15)
#define	REG_O5	(16)
#define	REG_O6	(17)
#define	REG_O7	(18)
#if defined(__sparcv9)
#define	REG_ASI	(19)
#define	REG_FPRS (20)
#endif	/* __sparcv9 */

/* the following defines are for portability */
#if !defined(__sparcv9)
#define	REG_PS	REG_PSR
#endif	/* __sparcv9 */
#define	REG_SP	REG_O6
#define	REG_R0	REG_O0
#define	REG_R1	REG_O1

#ifndef	_ASM

#define	NGREG	_NGREG

/*
 * The following structures define how a register window can appear on the
 * stack. This structure is available (when required) through the `gwins'
 * field of an mcontext (nested within ucontext). SPARC_MAXWINDOW is the
 * maximum number of outstanding regiters window defined in the SPARC
 * architecture (*not* implementation).
 */
#define	SPARC_MAXREGWINDOW	31	/* max windows in SPARC arch. */

struct	_rwindow {
	greg_t	rw_local[8];		/* locals */
	greg_t	rw_in[8];		/* ins */
};

#if defined(_SYSCALL32)

struct rwindow32 {
	greg32_t rw_local[8];		/* locals */
	greg32_t rw_in[8];		/* ins */
};

struct rwindow64 {
	greg64_t rw_local[8];		/* locals */
	greg64_t rw_in[8];		/* ins */
};

#if defined(_KERNEL)
extern	void	rwindow_nto32(struct _rwindow *, struct rwindow32 *);
extern	void	rwindow_32ton(struct rwindow32 *, struct _rwindow *);
#endif

#endif	/* _SYSCALL32 */

#define	rw_fp	rw_in[6]		/* frame pointer */
#define	rw_rtn	rw_in[7]		/* return address */

typedef struct _gwindows {
	int		wbcnt;
	greg_t		*spbuf[SPARC_MAXREGWINDOW];
	struct _rwindow	wbuf[SPARC_MAXREGWINDOW];
} gwindows_t;

#if defined(_SYSCALL32)

typedef struct gwindows32 {
	int32_t		wbcnt;
	caddr32_t	spbuf[SPARC_MAXREGWINDOW];
	struct rwindow32 wbuf[SPARC_MAXREGWINDOW];
} gwindows32_t;

typedef struct gwindows64 {
	int		wbcnt;
	greg64_t	*spbuf[SPARC_MAXREGWINDOW];
	struct rwindow64 wbuf[SPARC_MAXREGWINDOW];
} gwindows64_t;

#endif	/* _SYSCALL32 */

#endif	/* _ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_REGSET_H */
