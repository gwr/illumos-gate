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
 * Verify that user-level mutex_lock() and mutex_unlock() use the common mutex
 * model.  Paired unlocked and locally locked reads test direct operations,
 * while the acquire helper shows that a held-lock effect propagates to its
 * caller and is still diagnosed at the helper's own return.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct user_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(user_state::lock, user_state::value))

extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);
extern int user_mutex_unlocked(struct user_state *);
extern int user_mutex_locked(struct user_state *);
extern void user_mutex_acquire(struct user_state *);
extern int user_mutex_call_effect(struct user_state *);

int
user_mutex_unlocked(struct user_state *state)
{
	return (state->value);
}

int
user_mutex_locked(struct user_state *state)
{
	int value;

	(void) mutex_lock(&state->lock);
	value = state->value;
	(void) mutex_unlock(&state->lock);
	return (value);
}

void
user_mutex_acquire(struct user_state *state)
{
	(void) mutex_lock(&state->lock);
}

int
user_mutex_call_effect(struct user_state *state)
{
	int value;

	user_mutex_acquire(state);
	value = state->value;
	(void) mutex_unlock(&state->lock);
	return (value);
}
