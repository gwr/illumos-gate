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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Verify inference-first validation of declared competition side effects.
 * Exact net changes satisfy declarations; conditional, missing, opposite,
 * and non-returning implementations do not.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

static void
declared_enter(void)
{
	_NOTE(COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_NOW)
}

static void
declared_leave(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
declared_nested_enter(void)
{
	_NOTE(COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
declared_nested_leave(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
declared_nested_leave_caller(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	declared_nested_leave();
}

static void
declared_leave_wrapper(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	declared_leave();
}

static void
declared_missing(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
}

static void
declared_conditional(int leave)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	if (leave)
		_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
declared_opposite(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_NOW)
}

static void
declared_enter_conditional(int enter)
{
	_NOTE(COMPETING_THREADS_AS_SIDE_EFFECT)
	if (enter)
		_NOTE(COMPETING_THREADS_NOW)
}

static void
declared_nonreturning(void)
{
	_NOTE(COMPETING_THREADS_AS_SIDE_EFFECT)
	for (;;)
		;
}

static void
undeclared_effects(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(COMPETING_THREADS_NOW)
}
