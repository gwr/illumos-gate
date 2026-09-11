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
 * Characterize result-sensitive user mutex_lock() acquisition, whose normal
 * success return is zero, while preserving common ignored-result behavior.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct mutex_lock_state {
	mutex_t first;
	mutex_t second;
	int value;
};

_NOTE(LOCK_ORDER(mutex_lock_state::first mutex_lock_state::second))
_NOTE(MUTEX_PROTECTS_DATA(mutex_lock_state::first, mutex_lock_state::value))

extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);

static int
lock_equal_zero(struct mutex_lock_state *state)
{
	int value = 0;

	if (mutex_lock(&state->first) == 0) {
		value = state->value;
		(void) mutex_unlock(&state->first);
	} else {
		value = state->value;
	}
	return (value);
}

static int
lock_not_equal_zero(struct mutex_lock_state *state)
{
	int value = 0;

	if (mutex_lock(&state->first) != 0) {
		value = state->value;
	} else {
		value = state->value;
		(void) mutex_unlock(&state->first);
	}
	return (value);
}

static int
lock_truth(struct mutex_lock_state *state)
{
	int value = 0;

	if (mutex_lock(&state->first)) {
		value = state->value;
	} else {
		value = state->value;
		(void) mutex_unlock(&state->first);
	}
	return (value);
}

static int
lock_negated(struct mutex_lock_state *state)
{
	int value = 0;

	if (!mutex_lock(&state->first)) {
		value = state->value;
		(void) mutex_unlock(&state->first);
	} else {
		value = state->value;
	}
	return (value);
}

static int
lock_saved_result(struct mutex_lock_state *state)
{
	int error;
	int value = 0;

	error = mutex_lock(&state->first);
	if (error == 0) {
		value = state->value;
		(void) mutex_unlock(&state->first);
	}
	return (value);
}

static int
lock_ignored_result(struct mutex_lock_state *state)
{
	int value;

	(void) mutex_lock(&state->first);
	value = state->value;
	(void) mutex_unlock(&state->first);
	return (value);
}

static void
balanced_mutex_lock(struct mutex_lock_state *state)
{
	if (mutex_lock(&state->first) == 0)
		(void) mutex_unlock(&state->first);
}

static void
call_balanced_mutex_lock(struct mutex_lock_state *state)
{
	balanced_mutex_lock(state);
	state->value = 1;
}

static void
lock_order_on_success(struct mutex_lock_state *state)
{
	(void) mutex_lock(&state->second);
	if (mutex_lock(&state->first) == 0)
		(void) mutex_unlock(&state->first);
	(void) mutex_unlock(&state->second);
}

static int
lock_already_held(struct mutex_lock_state *state)
{
	int value;

	(void) mutex_lock(&state->first);
	if (mutex_lock(&state->first) == 0)
		(void) mutex_unlock(&state->first);
	value = state->value;
	(void) mutex_unlock(&state->first);
	return (value);
}

static int
return_mutex_lock(mutex_t *lock)
{
	return (mutex_lock(lock));
}

static void
call_returned_mutex_lock(struct mutex_lock_state *state)
{
	int error;

	error = return_mutex_lock(&state->first);
	state->value = 1;
	if (error == 0)
		(void) mutex_unlock(&state->first);
}
