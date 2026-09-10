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
 * Characterize acquisition-prefix state when acquired and released formal
 * roles map to either one actual lock or distinct locks.  A target lock that
 * precedes both roles in declared order exposes whether either role remains
 * held when the target is acquired.
 */

#ifdef __lock_lint
#include <sys/note.h>
#include <synch.h>
#else
#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;
#endif

struct prefix_alias_state {
	mutex_t first;
	mutex_t second;
	mutex_t third;
};

_NOTE(LOCK_ORDER(prefix_alias_state::first prefix_alias_state::second
    prefix_alias_state::third))

#ifndef ACQUISITION_PREFIX_ALIAS_VARIANT
#define	ACQUISITION_PREFIX_ALIAS_VARIANT	0
#endif

extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);

static void
release_before_target(mutex_t *released, mutex_t *acquired, mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	(void) mutex_unlock(released);
	(void) mutex_lock(target);
	(void) mutex_unlock(target);
	(void) mutex_lock(acquired);
}

static void
target_before_release(mutex_t *released, mutex_t *acquired, mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	(void) mutex_lock(target);
	(void) mutex_unlock(target);
	(void) mutex_unlock(released);
	(void) mutex_lock(acquired);
}

#if ACQUISITION_PREFIX_ALIAS_VARIANT == 1

static void
same_release_before_target(struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	release_before_target(&state->second, &state->second, &state->first);
	(void) mutex_unlock(&state->second);
}

static void
same_target_before_release(struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	target_before_release(&state->second, &state->second, &state->first);
	(void) mutex_unlock(&state->second);
}

#elif ACQUISITION_PREFIX_ALIAS_VARIANT == 2

static void
distinct_release_before_target(struct prefix_alias_state *state)
{
#ifdef __lock_lint
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->second))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->third))
#endif
	(void) mutex_lock(&state->second);
	release_before_target(&state->second, &state->third, &state->first);
	(void) mutex_unlock(&state->third);
}

static void
distinct_target_before_release(struct prefix_alias_state *state)
{
#ifdef __lock_lint
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->second))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->third))
#endif
	(void) mutex_lock(&state->second);
	target_before_release(&state->second, &state->third, &state->first);
	(void) mutex_unlock(&state->third);
}

#else
#error "unsupported ACQUISITION_PREFIX_ALIAS_VARIANT"
#endif
