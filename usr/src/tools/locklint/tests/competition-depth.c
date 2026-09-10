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
 * Verify that competition annotations change nesting depth rather than
 * assigning a Boolean state.  Functions begin with either zero or one
 * implicit competing level.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

struct competition_object {
	mutex_t lock;
	int protected;
	int read_only;
};

static struct competition_object competition_object;

_NOTE(MUTEX_PROTECTS_DATA(competition_object::lock,
    competition_object::protected))
_NOTE(READ_ONLY_DATA(competition_object::read_only))

static void
nested_competition(int value)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	competition_object.protected = value;
	_NOTE(COMPETING_THREADS_NOW)
	competition_object.protected = value;
	_NOTE(COMPETING_THREADS_NOW)
	competition_object.protected = value;
	_NOTE(NO_COMPETING_THREADS_NOW)
	competition_object.protected = value;
	_NOTE(NO_COMPETING_THREADS_NOW)
	competition_object.protected = value;
}

static void
equal_branch_depth(int value, int first)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	if (first) {
		_NOTE(COMPETING_THREADS_NOW)
		_NOTE(COMPETING_THREADS_NOW)
	} else {
		_NOTE(COMPETING_THREADS_NOW)
		_NOTE(COMPETING_THREADS_NOW)
	}
	competition_object.protected = value;
}

static void
different_branch_depth(int value, int first)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	if (first) {
		_NOTE(COMPETING_THREADS_NOW)
		_NOTE(COMPETING_THREADS_NOW)
	} else {
		_NOTE(COMPETING_THREADS_NOW)
	}
	competition_object.protected = value;
}

static void
unmatched_leaves(int value)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	competition_object.protected = value;
}

static void
growing_loop(int value, int repeat)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	while (repeat) {
		_NOTE(COMPETING_THREADS_NOW)
		repeat--;
	}
	competition_object.protected = value;
}

static void
definitely_competing_loop(int value, int repeat)
{
	_NOTE(COMPETING_THREADS_NOW)
	while (repeat) {
		_NOTE(COMPETING_THREADS_NOW)
		repeat--;
	}
	competition_object.protected = value;
}

static void
read_only_depth(int value)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	competition_object.read_only = value;
	_NOTE(COMPETING_THREADS_NOW)
	competition_object.read_only = value;
	_NOTE(COMPETING_THREADS_NOW)
	competition_object.read_only = value;
	_NOTE(NO_COMPETING_THREADS_NOW)
	competition_object.read_only = value;
	_NOTE(NO_COMPETING_THREADS_NOW)
	competition_object.read_only = value;
}

static void
underflow_recovery(int value)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(COMPETING_THREADS_NOW)
	competition_object.protected = value;
	_NOTE(COMPETING_THREADS_NOW)
	competition_object.protected = value;
}

static void
decreasing_loop(int value, int repeat)
{
	_NOTE(COMPETING_THREADS_NOW)
	while (repeat) {
		_NOTE(NO_COMPETING_THREADS_NOW)
		repeat--;
	}
	competition_object.protected = value;
}

static void
absolute_refinement(int value, int repeat)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS)
	competition_object.protected = value;

	_NOTE(COMPETING_THREADS_NOW)
	while (repeat) {
		_NOTE(COMPETING_THREADS_NOW)
		repeat--;
	}
	_NOTE(NO_COMPETING_THREADS)
	competition_object.protected = value;
}
