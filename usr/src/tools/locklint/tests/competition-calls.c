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
 * Verify inferred competition-depth effects through direct calls, wrappers,
 * branches, and recursion.  Invalid decrements are deferred through helpers
 * and diagnosed where caller state is available.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

struct competition_call_state {
	mutex_t lock;
	int protected;
};

_NOTE(MUTEX_PROTECTS_DATA(competition_call_state::lock,
    competition_call_state::protected))

static void
enter_competition(void)
{
	_NOTE(COMPETING_THREADS_NOW)
}

static void
leave_competition(void)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
balanced_region(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
quiet_publish(void)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(COMPETING_THREADS_NOW)
}

static void
enter_wrapper(void)
{
	enter_competition();
}

static void
conditional_enter(int enter)
{
	if (enter)
		_NOTE(COMPETING_THREADS_NOW)
}

static void
conditional_leave(int leave)
{
	if (leave)
		_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
recursive_enter(int depth)
{
	if (depth == 0)
		return;
	recursive_enter(depth - 1);
	_NOTE(COMPETING_THREADS_NOW)
}

static void
double_leave_wrapper(void)
{
	leave_competition();
	leave_competition();
}

static void
direct_enter_caller(struct competition_call_state *state)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	enter_competition();
	state->protected = 1;
}

static void
direct_leave_caller(struct competition_call_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	leave_competition();
	state->protected = 1;
}

static void
balanced_caller(struct competition_call_state *state)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	balanced_region();
	state->protected = 1;
}

static void
ambient_balanced_caller(struct competition_call_state *state)
{
	quiet_publish();
	state->protected = 1;
}

static void
conditional_enter_caller(struct competition_call_state *state, int enter)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	conditional_enter(enter);
	state->protected = 1;
}

static void
wrapped_enter_caller(struct competition_call_state *state)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	enter_wrapper();
	state->protected = 1;
}

static void
recursive_enter_caller(struct competition_call_state *state, int depth)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	recursive_enter(depth);
	state->protected = 1;
}

static void
invalid_leave_caller(void)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	leave_competition();
}

static void
conditional_invalid_leave_caller(int leave)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	conditional_leave(leave);
}

static void
wrapped_invalid_leave_caller(void)
{
	double_leave_wrapper();
}

static void
local_invalid_leave(void)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
anchored_underflow(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
ambient_anchored_underflow_caller(void)
{
	anchored_underflow();
}

static void
nonreturn_underflow(int spin)
{
	if (!spin)
		return;
	_NOTE(NO_COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	for (;;)
		;
}

static void
nonreturn_underflow_caller(void)
{
	nonreturn_underflow(1);
}

static void
recursive_shift(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	recursive_shift();
}

static void mutual_shift_right(void);

static void
mutual_shift_left(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	mutual_shift_right();
}

static void
mutual_shift_right(void)
{
	mutual_shift_left();
}
