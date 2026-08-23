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
 * This is the intraprocedural locking test.  It covers:
 *
 * protected access before acquisition and after release;
 * protected access while the mutex is held;
 * an unrelated member;
 * divergent branches that merge to uncertain lock state;
 * loops; and
 * conditional lock state at function return.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct check_state {
	int value;
	int other;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(check_state::lock, check_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
check_straight(struct check_state *state)
{
	int value;

	value = state->value;
	mutex_enter(&state->lock);
	value += state->value;
	value += state->other;
	mutex_exit(&state->lock);

	return (value + state->value);
}

static int
check_merge(struct check_state *state, int take_lock)
{
	int value;

	if (take_lock)
		mutex_enter(&state->lock);
	value = state->value;
	mutex_exit(&state->lock);

	return (value);
}

static int
check_loop(struct check_state *state, int count)
{
	int value = 0;

	mutex_enter(&state->lock);
	while (count-- != 0)
		value += state->value;
	mutex_exit(&state->lock);

	return (value);
}

static void
check_side_effect(struct check_state *state, int take_lock)
{
	if (take_lock)
		mutex_enter(&state->lock);
}
