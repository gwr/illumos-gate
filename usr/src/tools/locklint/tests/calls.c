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
 * This test covers entry lock conditions propagated through direct calls.
 * Its functions exercise direct and transitive callees, recursion, and
 * conservative analysis roots.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct call_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(call_state::lock, call_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
check_callee(struct call_state *state)
{
	return (state->value);
}

static int
check_locked_caller(struct call_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = check_callee(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
check_unlocked_caller(struct call_state *state)
{
	return (check_callee(state));
}

static int
check_wrapper(struct call_state *state)
{
	return (check_callee(state));
}

static int
check_locked_wrapper_caller(struct call_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = check_wrapper(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
check_unlocked_wrapper_caller(struct call_state *state)
{
	return (check_wrapper(state));
}

static int
check_recursive(struct call_state *state, int depth)
{
	if (depth != 0)
		return (check_recursive(state, depth - 1));
	return (state->value);
}

static int
check_recursive_caller(struct call_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = check_recursive(state, 2);
	mutex_exit(&state->lock);
	return (value);
}

static int
check_isolated_recursive(struct call_state *state, int depth)
{
	if (depth != 0)
		return (check_isolated_recursive(state, depth - 1));
	return (state->value);
}

static int check_cycle_right(int);

static int
check_cycle_left(int depth)
{
	if (depth == 0)
		return (0);
	return (check_cycle_right(depth - 1));
}

static int
check_cycle_right(int depth)
{
	if (depth == 0)
		return (0);
	return (check_cycle_left(depth - 1));
}

static int
check_no_competition_caller(struct call_state *state)
{
	_NOTE(NO_COMPETING_THREADS_NOW)
	return (check_wrapper(state));
}

static int
check_invisible_caller(struct call_state *state)
{
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->value))
	return (check_wrapper(state));
}

static int
check_competition_merge_caller(struct call_state *state, int quiet)
{
	if (quiet) {
		_NOTE(NO_COMPETING_THREADS_NOW)
	} else {
		_NOTE(COMPETING_THREADS_NOW)
	}
	return (check_callee(state));
}

static int
check_visibility_merge_caller(struct call_state *state, int invisible)
{
	if (invisible) {
		_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->value))
	} else {
		_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(state->value))
	}
	return (check_callee(state));
}

static int
check_lock_merge_caller(struct call_state *state, int take_lock)
{
	if (take_lock)
		mutex_enter(&state->lock);
	return (check_callee(state));
}
