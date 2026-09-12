/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Give each including translation unit a distinguishable GNU extern-inline
 * implementation.  Locklint must retain the implementation belonging to the
 * caller while treating the function name as one external linkage identity.
 */

#ifndef GNU_EXTERN_INLINE_VALUE
#error "GNU_EXTERN_INLINE_VALUE must identify the translation unit"
#endif

#include "gnu-extern-inline-decl.h"

extern __inline__ __attribute__((__gnu_inline__)) int
gnu_extern_inline_target(void)
{
	return (GNU_EXTERN_INLINE_VALUE);
}
