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
 * Characterize ordered lock effects when two formal roles map to either the
 * same caller lock or distinct locks.  Follow-up operations expose both the
 * final state and invalid intermediate operations.
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

#ifndef ALIAS_COMPOSITION_VARIANT
#define	ALIAS_COMPOSITION_VARIANT	0
#endif

extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);

static void
release_then_acquire(mutex_t *released, mutex_t *acquired)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	(void) mutex_unlock(released);
	(void) mutex_lock(acquired);
}

static void
acquire_then_release(mutex_t *acquired, mutex_t *released)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	(void) mutex_lock(acquired);
	(void) mutex_unlock(released);
}

#if ALIAS_COMPOSITION_VARIANT == 3 || ALIAS_COMPOSITION_VARIANT == 4

static void
release_then_acquire_wrapper(mutex_t *released, mutex_t *acquired)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	release_then_acquire(released, acquired);
}

static void
acquire_then_release_wrapper(mutex_t *acquired, mutex_t *released)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*acquired))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*released))
	acquire_then_release(acquired, released);
}

#endif

#if ALIAS_COMPOSITION_VARIANT == 5

static void
release_then_acquire_internal_wrapper(mutex_t *lock)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*lock))
	release_then_acquire(lock, lock);
	(void) mutex_unlock(lock);
}

static void
acquire_then_release_internal_wrapper(mutex_t *lock)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*lock))
	acquire_then_release(lock, lock);
	(void) mutex_lock(lock);
}

#endif

#if ALIAS_COMPOSITION_VARIANT == 1

static void
same_release_then_acquire(mutex_t *lock)
{
	(void) mutex_lock(lock);
	release_then_acquire(lock, lock);
	(void) mutex_unlock(lock);
}

static void
same_acquire_then_release(mutex_t *lock)
{
	acquire_then_release(lock, lock);
	(void) mutex_lock(lock);
	(void) mutex_unlock(lock);
}

#elif ALIAS_COMPOSITION_VARIANT == 2

static void
distinct_release_then_acquire(mutex_t *first, mutex_t *second)
{
	(void) mutex_lock(first);
	release_then_acquire(first, second);
	(void) mutex_unlock(second);
}

static void
distinct_acquire_then_release(mutex_t *first, mutex_t *second)
{
	(void) mutex_lock(second);
	acquire_then_release(first, second);
	(void) mutex_unlock(first);
}

#elif ALIAS_COMPOSITION_VARIANT == 3

static void
same_wrapped_release_then_acquire(mutex_t *lock)
{
	(void) mutex_lock(lock);
	release_then_acquire_wrapper(lock, lock);
	(void) mutex_unlock(lock);
}

static void
same_wrapped_acquire_then_release(mutex_t *lock)
{
	acquire_then_release_wrapper(lock, lock);
	(void) mutex_lock(lock);
	(void) mutex_unlock(lock);
}

#elif ALIAS_COMPOSITION_VARIANT == 4

static void
distinct_wrapped_release_then_acquire(mutex_t *first, mutex_t *second)
{
	(void) mutex_lock(first);
	release_then_acquire_wrapper(first, second);
	(void) mutex_unlock(second);
}

static void
distinct_wrapped_acquire_then_release(mutex_t *first, mutex_t *second)
{
	(void) mutex_lock(second);
	acquire_then_release_wrapper(first, second);
	(void) mutex_unlock(first);
}

#elif ALIAS_COMPOSITION_VARIANT == 5

static void
internal_release_then_acquire(mutex_t *lock)
{
	(void) mutex_lock(lock);
	release_then_acquire_internal_wrapper(lock);
	(void) mutex_lock(lock);
	(void) mutex_unlock(lock);
}

static void
internal_acquire_then_release(mutex_t *lock)
{
	acquire_then_release_internal_wrapper(lock);
	(void) mutex_unlock(lock);
}

#else
#error "unsupported ALIAS_COMPOSITION_VARIANT"
#endif
