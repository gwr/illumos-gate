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
 * Model the common header-before-caller arrangement, in which Sparse may
 * expand the translation-unit inline body directly into its caller.
 */

#define	GNU_EXTERN_INLINE_VALUE	6
#include "gnu-extern-inline.h"

static int
gnu_extern_inline_body_first(void)
{
	return (gnu_extern_inline_target());
}
