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
 * Verify function-relative and absolute declared lock-side-effect
 * expressions.
 */

#define	_NOTE(arg)

typedef int mutex_t;
typedef int rwlock_t;

struct declared_effect_state {
	mutex_t mutex;
	rwlock_t rwlock;
};

static mutex_t absolute_mutex;
static rwlock_t absolute_rwlock;

static void
formal_effects(struct declared_effect_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->mutex))
	_NOTE(READ_LOCK_ACQUIRED_AS_SIDE_EFFECT(state->rwlock))
	_NOTE(WRITE_LOCK_ACQUIRED_AS_SIDE_EFFECT(state->rwlock))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->rwlock))
	_NOTE(LOCK_UPGRADED_AS_SIDE_EFFECT(state->rwlock))
	_NOTE(LOCK_DOWNGRADED_AS_SIDE_EFFECT(state->rwlock))
}

static void
absolute_effects(void)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(absolute_mutex))
	_NOTE(READ_LOCK_ACQUIRED_AS_SIDE_EFFECT(absolute_rwlock))
	_NOTE(WRITE_LOCK_ACQUIRED_AS_SIDE_EFFECT(absolute_rwlock))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(absolute_mutex))
	_NOTE(LOCK_UPGRADED_AS_SIDE_EFFECT(absolute_rwlock))
	_NOTE(LOCK_DOWNGRADED_AS_SIDE_EFFECT(absolute_rwlock))
}
