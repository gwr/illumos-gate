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
#if ACQUISITION_PREFIX_ALIAS_VARIANT != 6
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
#endif
#if ACQUISITION_PREFIX_ALIAS_VARIANT == 3 || \
    ACQUISITION_PREFIX_ALIAS_VARIANT == 4

static void
release_before_target_wrapper(mutex_t *released, mutex_t *acquired,
    mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	release_before_target(released, acquired, target);
}

static void
target_before_release_wrapper(mutex_t *released, mutex_t *acquired,
    mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	target_before_release(released, acquired, target);
}

#endif

#if ACQUISITION_PREFIX_ALIAS_VARIANT == 5

static void
release_before_target_internal_wrapper(mutex_t *lock, mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*lock))
	release_before_target(lock, lock, target);
	(void) mutex_unlock(lock);
}

static void
target_before_release_internal_wrapper(mutex_t *lock, mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*lock))
	target_before_release(lock, lock, target);
	(void) mutex_unlock(lock);
}

#endif

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

#elif ACQUISITION_PREFIX_ALIAS_VARIANT == 3

static void
same_wrapped_release_before_target(struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	release_before_target_wrapper(&state->second, &state->second,
	    &state->first);
	(void) mutex_unlock(&state->second);
}

static void
same_wrapped_target_before_release(struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	target_before_release_wrapper(&state->second, &state->second,
	    &state->first);
	(void) mutex_unlock(&state->second);
}

#elif ACQUISITION_PREFIX_ALIAS_VARIANT == 4

static void
distinct_wrapped_release_before_target(struct prefix_alias_state *state)
{
#ifdef __lock_lint
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->second))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->third))
#endif
	(void) mutex_lock(&state->second);
	release_before_target_wrapper(&state->second, &state->third,
	    &state->first);
	(void) mutex_unlock(&state->third);
}

static void
distinct_wrapped_target_before_release(struct prefix_alias_state *state)
{
#ifdef __lock_lint
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->second))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->third))
#endif
	(void) mutex_lock(&state->second);
	target_before_release_wrapper(&state->second, &state->third,
	    &state->first);
	(void) mutex_unlock(&state->third);
}

#elif ACQUISITION_PREFIX_ALIAS_VARIANT == 5

static void
internal_release_before_target(struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	release_before_target_internal_wrapper(&state->second, &state->first);
	(void) mutex_lock(&state->second);
	(void) mutex_unlock(&state->second);
}

static void
internal_target_before_release(struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	target_before_release_internal_wrapper(&state->second, &state->first);
	(void) mutex_lock(&state->second);
	(void) mutex_unlock(&state->second);
}

#elif ACQUISITION_PREFIX_ALIAS_VARIANT == 6

static void
multiple_changes_before_target(mutex_t *first, mutex_t *second,
    mutex_t *third, mutex_t *fourth, mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*first))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*second))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*third))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*fourth))
	(void) mutex_unlock(first);
	(void) mutex_lock(second);
	(void) mutex_unlock(third);
	(void) mutex_lock(fourth);
	(void) mutex_lock(target);
	(void) mutex_unlock(target);
}

static void
multiple_changes_before_target_wrapper(mutex_t *first, mutex_t *second,
    mutex_t *third, mutex_t *fourth, mutex_t *target)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*first))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*second))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*third))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*fourth))
	multiple_changes_before_target(first, second, third, fourth, target);
}

static void
same_multiple_changes_before_target(struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	multiple_changes_before_target(&state->second, &state->second,
	    &state->second, &state->second, &state->first);
	(void) mutex_unlock(&state->second);
}

static void
same_wrapped_multiple_changes_before_target(
    struct prefix_alias_state *state)
{
	(void) mutex_lock(&state->second);
	multiple_changes_before_target_wrapper(&state->second, &state->second,
	    &state->second, &state->second, &state->first);
	(void) mutex_unlock(&state->second);
}

#else
#error "unsupported ACQUISITION_PREFIX_ALIAS_VARIANT"
#endif
