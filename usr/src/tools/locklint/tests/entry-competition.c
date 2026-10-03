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
 * Verify command-file competition conditions on declared entry points.
 * The helper is deliberately external to model a module-private call across
 * translation units without making that helper an independent entry point.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#include <sys/note.h>
#else
#define	_NOTE(arg)
#define	ASSERT(expr)
#endif

#define	NO_COMPETING_THREADS	1

void command_entry_helper(void);

int
_init(void)
{
	command_entry_helper();
	return (0);
}

int
_fini(void)
{
	return (0);
}

void
command_entry_unconfigured(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	ASSERT(NO_COMPETING_THREADS);
}
