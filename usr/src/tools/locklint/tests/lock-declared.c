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
 * Characterize declared lock effects and asserted entry lock conditions.
 * The OSLL runner selects separate variants so one inconsistent contract
 * cannot obscure unrelated behavior.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#include <sys/note.h>
#include <synch.h>
#define	MUTEX_NOT_HELD(lock)	(!MUTEX_HELD(lock))
#else
#define	_NOTE(arg)
#define	ASSERT(expr)	((void)0)
#define	MUTEX_HELD(lock)	(1)
#define	MUTEX_NOT_HELD(lock)	(1)

typedef struct mutex {
	int opaque;
} mutex_t;
#endif

#ifndef LOCK_DECLARED_VARIANT
#define	LOCK_DECLARED_VARIANT	0
#endif

struct declared_state {
	mutex_t lock;
};

extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);

#if LOCK_DECLARED_VARIANT == 1

static void
declared_acquire_valid(struct declared_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->lock))
	(void) mutex_lock(&state->lock);
}

static void
call_declared_acquire_unheld(struct declared_state *state)
{
	declared_acquire_valid(state);
	(void) mutex_unlock(&state->lock);
}

static void
call_declared_acquire_held(struct declared_state *state)
{
	(void) mutex_lock(&state->lock);
	declared_acquire_valid(state);
	(void) mutex_unlock(&state->lock);
}

static void
call_declared_acquire_held_contract(struct declared_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->lock))
	(void) mutex_lock(&state->lock);
	declared_acquire_valid(state);
}

static void
declared_acquire_missing(struct declared_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->lock))
	(void) state;
}

static void
declared_acquire_conditional(struct declared_state *state, int acquire)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->lock))
	if (acquire)
		(void) mutex_lock(&state->lock);
}

static void
undeclared_acquire(struct declared_state *state)
{
	(void) mutex_lock(&state->lock);
}

static void
declared_acquire_duplicate(struct declared_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->lock))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->lock))
	(void) mutex_lock(&state->lock);
}

#elif LOCK_DECLARED_VARIANT == 2

static void
declared_release_valid(struct declared_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->lock))
	(void) mutex_unlock(&state->lock);
}

static void
call_declared_release_held(struct declared_state *state)
{
	(void) mutex_lock(&state->lock);
	declared_release_valid(state);
}

static void
call_declared_release_unheld(struct declared_state *state)
{
	declared_release_valid(state);
}

static void
call_declared_release_unheld_contract(struct declared_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->lock))
	declared_release_valid(state);
}

static void
declared_release_missing(struct declared_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->lock))
	(void) state;
}

static void
declared_release_conditional(struct declared_state *state, int release)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->lock))
	if (release)
		(void) mutex_unlock(&state->lock);
}

static void
undeclared_release(struct declared_state *state)
{
	(void) mutex_unlock(&state->lock);
}

#elif LOCK_DECLARED_VARIANT == 3

static void
require_held(struct declared_state *state)
{
	ASSERT(MUTEX_HELD(&state->lock));
}

static void
require_not_held(struct declared_state *state)
{
	ASSERT(MUTEX_NOT_HELD(&state->lock));
}

static void
assert_after_acquire(struct declared_state *state)
{
	(void) mutex_lock(&state->lock);
	ASSERT(MUTEX_HELD(&state->lock));
	(void) mutex_unlock(&state->lock);
}

static void
assert_held_conditionally(struct declared_state *state, int check)
{
	if (check)
		ASSERT(MUTEX_HELD(&state->lock));
}

static void
call_require_held_locked(struct declared_state *state)
{
	(void) mutex_lock(&state->lock);
	require_held(state);
	(void) mutex_unlock(&state->lock);
}

static void
call_require_held_unlocked(struct declared_state *state)
{
	require_held(state);
}

static void
call_require_not_held_unlocked(struct declared_state *state)
{
	require_not_held(state);
}

static void
call_require_not_held_locked(struct declared_state *state)
{
	(void) mutex_lock(&state->lock);
	require_not_held(state);
	(void) mutex_unlock(&state->lock);
}

static void
require_held_wrapper(struct declared_state *state)
{
	require_held(state);
}

static void
call_require_held_wrapper_unlocked(struct declared_state *state)
{
	require_held_wrapper(state);
}

#elif LOCK_DECLARED_VARIANT == 4

static void
declared_acquire_no_return(struct declared_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->lock))
	for (;;)
		;
}

static void
declared_release_no_return(struct declared_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->lock))
	for (;;)
		;
}

#endif
