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
 * This test covers mutex side-effect summaries.  It includes functions
 * that acquire, release, preserve, or conditionally change a formal
 * argument's lock, as well as transitive and recursive call chains.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct effect_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(effect_state::lock, effect_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
acquire_lock(struct effect_state *state)
{
	mutex_enter(&state->lock);
}

static void
acquire_wrapper(struct effect_state *state)
{
	acquire_lock(state);
}

static void
release_lock(struct effect_state *state)
{
	mutex_exit(&state->lock);
}

static void
balanced_lock(struct effect_state *state)
{
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
}

static void
conditional_acquire(struct effect_state *state, int take_lock)
{
	if (take_lock)
		mutex_enter(&state->lock);
}

static void
recursive_acquire(struct effect_state *state, int depth)
{
	if (depth != 0)
		recursive_acquire(state, depth - 1);
	else
		mutex_enter(&state->lock);
}

static int
check_acquire_effect(struct effect_state *state)
{
	int value;

	acquire_lock(state);
	value = state->value;
	mutex_exit(&state->lock);
	return (value);
}

static int
check_transitive_acquire(struct effect_state *state)
{
	int value;

	acquire_wrapper(state);
	value = state->value;
	mutex_exit(&state->lock);
	return (value);
}

static void
check_acquire_invalid(struct effect_state *state)
{
	mutex_enter(&state->lock);
	acquire_lock(state);
	mutex_exit(&state->lock);
}

static int
check_release_effect(struct effect_state *state)
{
	mutex_enter(&state->lock);
	release_lock(state);
	return (state->value);
}

static void
check_release_invalid(struct effect_state *state)
{
	release_lock(state);
}

static int
check_balanced_effect(struct effect_state *state)
{
	balanced_lock(state);
	return (state->value);
}

static int
check_conditional_effect(struct effect_state *state, int take_lock)
{
	conditional_acquire(state, take_lock);
	return (state->value);
}

static int
check_recursive_effect(struct effect_state *state)
{
	int value;

	recursive_acquire(state, 2);
	value = state->value;
	mutex_exit(&state->lock);
	return (value);
}
