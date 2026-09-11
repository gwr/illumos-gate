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
 * Characterize user mutex_trylock() as a zero-success conditional
 * acquisition, including ignored results, ordering, and held input.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct trylock_state {
	mutex_t first;
	mutex_t second;
	int value;
};

_NOTE(LOCK_ORDER(trylock_state::first trylock_state::second))
_NOTE(MUTEX_PROTECTS_DATA(trylock_state::first, trylock_state::value))

extern int mutex_lock(mutex_t *);
extern int mutex_trylock(mutex_t *);
extern int mutex_unlock(mutex_t *);

static int
trylock_equal_zero(struct trylock_state *state)
{
	int value = 0;

	if (mutex_trylock(&state->first) == 0) {
		value = state->value;
		(void) mutex_unlock(&state->first);
	} else {
		value = state->value;
	}
	return (value);
}

static int
trylock_not_equal_zero(struct trylock_state *state)
{
	int value = 0;

	if (mutex_trylock(&state->first) != 0) {
		value = state->value;
	} else {
		value = state->value;
		(void) mutex_unlock(&state->first);
	}
	return (value);
}

static int
trylock_truth(struct trylock_state *state)
{
	int value = 0;

	if (mutex_trylock(&state->first)) {
		value = state->value;
	} else {
		value = state->value;
		(void) mutex_unlock(&state->first);
	}
	return (value);
}

static int
trylock_negated(struct trylock_state *state)
{
	int value = 0;

	if (!mutex_trylock(&state->first)) {
		value = state->value;
		(void) mutex_unlock(&state->first);
	} else {
		value = state->value;
	}
	return (value);
}

static int
trylock_saved_result(struct trylock_state *state)
{
	int error;
	int value = 0;

	error = mutex_trylock(&state->first);
	if (error == 0) {
		value = state->value;
		(void) mutex_unlock(&state->first);
	}
	return (value);
}

static void
trylock_ignored_result(struct trylock_state *state)
{
	(void) mutex_trylock(&state->first);
	state->value = 1;
	(void) mutex_unlock(&state->first);
}

static void
call_balanced_trylock(struct trylock_state *state)
{
	(void) trylock_equal_zero(state);
	state->value = 1;
}

static void
trylock_order_on_success(struct trylock_state *state)
{
	(void) mutex_lock(&state->second);
	if (mutex_trylock(&state->first) == 0)
		(void) mutex_unlock(&state->first);
	(void) mutex_unlock(&state->second);
}

static int
trylock_already_held(struct trylock_state *state)
{
	int value;

	(void) mutex_lock(&state->first);
	if (mutex_trylock(&state->first) == 0)
		(void) mutex_unlock(&state->first);
	value = state->value;
	(void) mutex_unlock(&state->first);
	return (value);
}
