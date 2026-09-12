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
 * Verify that a call preceding a GNU extern-inline definition makes that
 * translation-unit implementation available without an address-taking use.
 */

#define	GNU_EXTERN_INLINE_VALUE	5
#include "gnu-extern-inline-decl.h"

static int
gnu_extern_inline_direct_only(void)
{
	return (gnu_extern_inline_target());
}

#include "gnu-extern-inline.h"
