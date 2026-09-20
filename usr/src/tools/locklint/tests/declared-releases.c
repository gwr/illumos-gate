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
 * Verify declared releases from exact held contract entries.  Contract-only
 * execution must not contribute ordinary transition diagnostics.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;

struct declared_release_state {
	mutex_t mutex;
	rwlock_t rwlock;
};

extern void mutex_exit(mutex_t *);
extern void rw_exit(rwlock_t *);

static void
declared_release_valid(struct declared_release_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->mutex))
	mutex_exit(&state->mutex);
}

static void
declared_release_missing(struct declared_release_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->mutex))
	(void) state;
}

static void
declared_release_conditional(struct declared_release_state *state, int release)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->rwlock))
	if (release)
		rw_exit(&state->rwlock);
}

static void
declared_release_nonreturning(struct declared_release_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->mutex))
	(void) state;
	for (;;)
		;
}
