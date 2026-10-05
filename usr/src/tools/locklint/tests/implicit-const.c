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
 * Characterize whether Old Solaris Lock Lint treats C const objects as
 * implicitly read-only.  Separately compiled variants distinguish genuine
 * const objects from mutable objects reached through const-qualified pointer
 * expressions.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

#include "implicit-const.h"

#ifndef IMPLICIT_CONST_VARIANT
#define	IMPLICIT_CONST_VARIANT	0
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

static mutex_t const_lock;
static int mutable_value;
static int explicit_read_only;
static int pointer_target;

_NOTE(MUTEX_PROTECTS_DATA(const_lock, mutable_value implicit_const_value
    implicit_const_record pointer_target))
_NOTE(READ_ONLY_DATA(explicit_read_only))

#if IMPLICIT_CONST_VARIANT == 1

int
implicit_const_mutable_control(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = mutable_value;
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (value);
}

#elif IMPLICIT_CONST_VARIANT == 2

int
implicit_const_explicit_control(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = explicit_read_only;
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (value);
}

#elif IMPLICIT_CONST_VARIANT == 3

int
implicit_const_scalar(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = implicit_const_value;
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (value);
}

#elif IMPLICIT_CONST_VARIANT == 4

int
implicit_const_aggregate(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = implicit_const_record.first;
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (value);
}

#elif IMPLICIT_CONST_VARIANT == 5

int
implicit_const_pointer_target(void)
{
	const int *view = &pointer_target;
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = *view;
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (value);
}

#elif IMPLICIT_CONST_VARIANT == 6

int
implicit_const_pointer(void)
{
	int *const view = &pointer_target;
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = *view;
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (value);
}

#elif IMPLICIT_CONST_VARIANT == 7

void
implicit_const_forced_write(int value)
{
	_NOTE(COMPETING_THREADS_NOW)
	*(int *)(void *)&implicit_const_value = value;
	_NOTE(NO_COMPETING_THREADS_NOW)
}

#elif IMPLICIT_CONST_VARIANT == 8

int
implicit_const_mutable_pointer_read(void)
{
	int *view = &pointer_target;
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = *view;
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (value);
}

#elif IMPLICIT_CONST_VARIANT == 9

void
implicit_const_mutable_pointer_write(int value)
{
	int *view = &pointer_target;

	_NOTE(COMPETING_THREADS_NOW)
	*view = value;
	_NOTE(NO_COMPETING_THREADS_NOW)
}

#else
#error "unknown implicit const characterization variant"
#endif
