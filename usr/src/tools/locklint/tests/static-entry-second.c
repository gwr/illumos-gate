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
 * Provide the same static callback name in another translation unit.  It
 * must remain unreachable when policy qualifies the first definition.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#else
#define	ASSERT(expr)
#endif

#define	NO_COMPETING_THREADS	1

static void
static_vector_callback(void)
{
	ASSERT(NO_COMPETING_THREADS);
}

void (*static_entry_second_vector)(void) = static_vector_callback;
