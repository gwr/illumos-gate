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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SA_STDDEF_H
#define	_SA_STDDEF_H

/*
 * Exported interfaces for standalone's subset of libc's <stddef.h>.
 * All standalone code *must* use this header rather than libc's.
 */

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	NULL
#define	NULL    0
#endif

#if !defined(_PTRDIFF_T)
#define	_PTRDIFF_T
#if defined(_LP64) || defined(_I32LPx)
typedef	long	ptrdiff_t;		/* pointer difference */
#else
typedef int	ptrdiff_t;		/* (historical version) */
#endif
#endif	/* !_PTRDIFF_T */

#if !defined(_SIZE_T)
#define	_SIZE_T
#if defined(_LP64) || defined(_I32LPx)
typedef unsigned long	size_t;		/* size of something in bytes */
#else
typedef unsigned int	size_t;		/* (historical version) */
#endif
#endif	/* !_SIZE_T */

#ifndef offsetof
#define	offsetof(s, m)  (size_t)(&(((s *)0)->m))
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SA_STDDEF_H */
