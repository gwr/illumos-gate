/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Characterize nested competition regions and declared competition side
 * effects.  The OSLL runner selects separate variants so one inconsistent
 * contract cannot obscure unrelated behavior.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

#ifndef COMPETITION_DECLARED_VARIANT
#define	COMPETITION_DECLARED_VARIANT	0
#endif

#if COMPETITION_DECLARED_VARIANT == 1

static void
competition_balanced(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
competition_nested_balanced(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
competition_nested_enter_effect(void)
{
	_NOTE(COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
competition_nested_leave_effect(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
competition_opposite_branches(int competing)
{
	if (competing) {
		_NOTE(COMPETING_THREADS_NOW)
	} else {
		_NOTE(NO_COMPETING_THREADS_NOW)
	}
}

static void
competition_unmatched_leave(void)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

#elif COMPETITION_DECLARED_VARIANT == 2

static void
declared_no_competition_valid(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
declared_no_competition_missing(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
}

static void
declared_no_competition_conditional(int quiet)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	if (quiet) {
		_NOTE(NO_COMPETING_THREADS_NOW)
	}
}

static void
undeclared_no_competition(void)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
declared_competition_valid(void)
{
	_NOTE(COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(COMPETING_THREADS_NOW)
}

static void
undeclared_competition(void)
{
	_NOTE(COMPETING_THREADS_NOW)
}

#elif COMPETITION_DECLARED_VARIANT == 3

static void
competition_leave_effect(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
call_competition_leave_contract(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	competition_leave_effect();
}

static void
call_competition_leave_no_contract(void)
{
	competition_leave_effect();
}

static void
competition_leave_wrapper(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	competition_leave_effect();
}

static void
call_competition_wrapper_contract(void)
{
	_NOTE(NO_COMPETING_THREADS_AS_SIDE_EFFECT)
	competition_leave_wrapper();
}

static void
call_competition_wrapper_no_contract(void)
{
	competition_leave_wrapper();
}

#endif
