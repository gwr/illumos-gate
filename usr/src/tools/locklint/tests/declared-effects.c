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
 * Verify declared effects on locks relative to formal arguments.  Undeclared
 * inferred effects remain valid and retain their ordinary return diagnostic.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;

struct declared_effect_state {
	mutex_t mutex;
	rwlock_t rwlock;
};

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void rw_rdlock(rwlock_t *);
extern void rw_wrlock(rwlock_t *);
extern void rw_exit(rwlock_t *);

static void
declared_mutex_valid(struct declared_effect_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->mutex))
	mutex_enter(&state->mutex);
}

static void
declared_mutex_missing(struct declared_effect_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->mutex))
	(void) state;
}

static void
declared_mutex_conditional(struct declared_effect_state *state, int acquire)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->mutex))
	if (acquire)
		mutex_enter(&state->mutex);
}

static void
declared_mutex_wrong_mode(struct declared_effect_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->rwlock))
	rw_rdlock(&state->rwlock);
}

static void
declared_read_valid(struct declared_effect_state *state)
{
	_NOTE(READ_LOCK_ACQUIRED_AS_SIDE_EFFECT(state->rwlock))
	rw_rdlock(&state->rwlock);
}

static void
declared_write_valid(struct declared_effect_state *state)
{
	_NOTE(WRITE_LOCK_ACQUIRED_AS_SIDE_EFFECT(state->rwlock))
	rw_wrlock(&state->rwlock);
}

static void
declared_read_wrong_mode(struct declared_effect_state *state)
{
	_NOTE(READ_LOCK_ACQUIRED_AS_SIDE_EFFECT(state->rwlock))
	rw_wrlock(&state->rwlock);
}

static void
declared_release_valid(struct declared_effect_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->mutex))
	mutex_exit(&state->mutex);
}

static void
declared_release_missing(struct declared_effect_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->mutex))
	(void) state;
}

static void
declared_release_conditional(struct declared_effect_state *state, int release)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->rwlock))
	if (release)
		rw_exit(&state->rwlock);
}

static void
undeclared_mutex_acquire(struct declared_effect_state *state)
{
	mutex_enter(&state->mutex);
}
