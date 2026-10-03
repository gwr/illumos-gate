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
 * Provide the cross-translation-unit helper used to verify that a declared
 * entry condition propagates through an ordinary resolved call.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#else
#define	ASSERT(expr)
#endif

#define	NO_COMPETING_THREADS	1

void
command_entry_helper(void)
{
	ASSERT(NO_COMPETING_THREADS);
}
