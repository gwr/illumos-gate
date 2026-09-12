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
 * Verify that direct calls can use a GNU extern-inline implementation while
 * taking its address does not invent an emitted external definition.
 */

#define	GNU_EXTERN_INLINE_VALUE	4
#include "gnu-extern-inline-decl.h"

static int
gnu_extern_inline_no_external(void)
{
	return (gnu_extern_inline_target());
}

#include "gnu-extern-inline.h"

static int (*gnu_extern_inline_unresolved_pointer)(void) =
    gnu_extern_inline_target;
