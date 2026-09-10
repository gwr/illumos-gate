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
 * Characterize asserted entry requirements when distinct formal lock roles
 * map to one actual lock.  The variants separate valid same-actual and
 * distinct-actual calls from deliberately incompatible requirements.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#include <synch.h>
#else
#define	ASSERT(expr)
#define	MUTEX_HELD(lock)	mutex_owned(lock)

typedef struct mutex {
	int opaque;
} mutex_t;
#endif

struct assertion_alias_state {
	mutex_t lock;
};

#ifndef ASSERTION_ALIAS_VARIANT
#define	ASSERTION_ALIAS_VARIANT	0
#endif

extern int mutex_owned(mutex_t *);
extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);

#if ASSERTION_ALIAS_VARIANT == 1 || ASSERTION_ALIAS_VARIANT == 2 || \
    ASSERTION_ALIAS_VARIANT == 4 || ASSERTION_ALIAS_VARIANT == 5 || \
    ASSERTION_ALIAS_VARIANT == 6

static void
acquire_first_require_second(struct assertion_alias_state *first,
    struct assertion_alias_state *second)
{
	(void) mutex_lock(&first->lock);
	ASSERT(MUTEX_HELD(&second->lock));
	(void) mutex_unlock(&first->lock);
}

#endif

#if ASSERTION_ALIAS_VARIANT == 4 || ASSERTION_ALIAS_VARIANT == 5

static void
acquire_first_require_second_wrapper(struct assertion_alias_state *first,
    struct assertion_alias_state *second)
{
	acquire_first_require_second(first, second);
}

#endif

#if ASSERTION_ALIAS_VARIANT == 6

static void
acquire_satisfies_internal_alias(struct assertion_alias_state *state)
{
	acquire_first_require_second(state, state);
}

#endif

#if ASSERTION_ALIAS_VARIANT == 1

static void
require_both_held(struct assertion_alias_state *first,
    struct assertion_alias_state *second)
{
	ASSERT(MUTEX_HELD(&first->lock));
	ASSERT(MUTEX_HELD(&second->lock));
}

#endif

#if ASSERTION_ALIAS_VARIANT == 2 || ASSERTION_ALIAS_VARIANT == 3

static void
require_held_and_not_held(struct assertion_alias_state *held,
    struct assertion_alias_state *not_held)
{
	ASSERT(MUTEX_HELD(&held->lock));
	ASSERT(!MUTEX_HELD(&not_held->lock));
}

#endif

#if ASSERTION_ALIAS_VARIANT == 1

static void
same_acquire_satisfies_requirement(struct assertion_alias_state *state)
{
	acquire_first_require_second(state, state);
}

static void
same_compatible_requirements(struct assertion_alias_state *state)
{
	(void) mutex_lock(&state->lock);
	require_both_held(state, state);
	(void) mutex_unlock(&state->lock);
}

#elif ASSERTION_ALIAS_VARIANT == 2

static void
distinct_acquire_does_not_satisfy_requirement(
    struct assertion_alias_state *first,
    struct assertion_alias_state *second)
{
	acquire_first_require_second(first, second);
}

static void
distinct_opposite_requirements(struct assertion_alias_state *first,
    struct assertion_alias_state *second)
{
	(void) mutex_lock(&first->lock);
	require_held_and_not_held(first, second);
	(void) mutex_unlock(&first->lock);
}

#elif ASSERTION_ALIAS_VARIANT == 3

static void
same_opposite_requirements_unheld(struct assertion_alias_state *state)
{
	require_held_and_not_held(state, state);
}

static void
same_opposite_requirements_held(struct assertion_alias_state *state)
{
	(void) mutex_lock(&state->lock);
	require_held_and_not_held(state, state);
	(void) mutex_unlock(&state->lock);
}

#elif ASSERTION_ALIAS_VARIANT == 4

static void
same_acquire_satisfies_wrapped_requirement(
    struct assertion_alias_state *state)
{
	acquire_first_require_second_wrapper(state, state);
}

#elif ASSERTION_ALIAS_VARIANT == 5

static void
distinct_acquire_does_not_satisfy_wrapped_requirement(
    struct assertion_alias_state *first,
    struct assertion_alias_state *second)
{
	acquire_first_require_second_wrapper(first, second);
}

#elif ASSERTION_ALIAS_VARIANT == 6

static void
call_internal_assertion_alias(struct assertion_alias_state *state)
{
	acquire_satisfies_internal_alias(state);
}

#elif ASSERTION_ALIAS_VARIANT == 7

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

static void
acquire_first_release_second_require_first(
    struct assertion_alias_state *first,
    struct assertion_alias_state *second)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(first->lock))
	(void) mutex_lock(&first->lock);
	(void) mutex_unlock(&second->lock);
	ASSERT(MUTEX_HELD(&first->lock));
}

static void
same_release_invalidates_requirement(struct assertion_alias_state *state)
{
	acquire_first_release_second_require_first(state, state);
}

#elif ASSERTION_ALIAS_VARIANT == 8

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

static void
acquire_release_acquire_require_fourth(
    mutex_t *first, mutex_t *second, mutex_t *third,
    struct assertion_alias_state *required)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*first))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*second))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*third))
	(void) mutex_lock(first);
	(void) mutex_unlock(second);
	(void) mutex_lock(third);
	ASSERT(MUTEX_HELD(&required->lock));
}

static void
multiple_assertion_alias_wrapper(mutex_t *first, mutex_t *second,
    mutex_t *third,
    struct assertion_alias_state *required)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*first))
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(*second))
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(*third))
	acquire_release_acquire_require_fourth(first, second, third, required);
}

static void
same_multiple_assertion_alias_direct(struct assertion_alias_state *state)
{
	acquire_release_acquire_require_fourth(&state->lock, &state->lock,
	    &state->lock, state);
	(void) mutex_unlock(&state->lock);
}

static void
same_multiple_assertion_alias_wrapped(struct assertion_alias_state *state)
{
	multiple_assertion_alias_wrapper(&state->lock, &state->lock,
	    &state->lock, state);
	(void) mutex_unlock(&state->lock);
}

static void
distinct_required_multiple_assertion_alias_wrapped(
    struct assertion_alias_state *shared,
    struct assertion_alias_state *required)
{
	multiple_assertion_alias_wrapper(&shared->lock, &shared->lock,
	    &shared->lock, required);
	(void) mutex_unlock(&shared->lock);
}

#elif ASSERTION_ALIAS_VARIANT == 9

static void
merged_alias_leaf(mutex_t *acquired, struct assertion_alias_state *required)
{
	(void) mutex_lock(acquired);
	ASSERT(MUTEX_HELD(&required->lock));
	(void) mutex_unlock(acquired);
}

static void
merged_alias_wrapper(mutex_t *first, mutex_t *second,
    struct assertion_alias_state *required, int select)
{
	if (select)
		merged_alias_leaf(first, required);
	else
		merged_alias_leaf(second, required);
}

static void
same_merged_assertion_aliases(struct assertion_alias_state *state, int select)
{
	merged_alias_wrapper(&state->lock, &state->lock, state, select);
}

#elif ASSERTION_ALIAS_VARIANT == 10

static void
overflow_alias_leaf(mutex_t *first, mutex_t *second, mutex_t *third,
    mutex_t *fourth, mutex_t *fifth,
    struct assertion_alias_state *required)
{
	(void) mutex_lock(first);
	(void) mutex_lock(second);
	(void) mutex_lock(third);
	(void) mutex_lock(fourth);
	(void) mutex_lock(fifth);
	ASSERT(MUTEX_HELD(&required->lock));
	(void) mutex_unlock(fifth);
	(void) mutex_unlock(fourth);
	(void) mutex_unlock(third);
	(void) mutex_unlock(second);
	(void) mutex_unlock(first);
}

static void
overflow_alias_wrapper(mutex_t *first, mutex_t *second, mutex_t *third,
    mutex_t *fourth, mutex_t *fifth, mutex_t *sixth, mutex_t *seventh,
    mutex_t *eighth, mutex_t *ninth,
    struct assertion_alias_state *required, int select)
{
	if (select) {
		overflow_alias_leaf(first, second, third, fourth, fifth,
		    required);
	} else {
		overflow_alias_leaf(fifth, sixth, seventh, eighth, ninth,
		    required);
	}
}

static void
check_assertion_alias_overflow(mutex_t *first, mutex_t *second,
    mutex_t *third, mutex_t *fourth, mutex_t *fifth, mutex_t *sixth,
    mutex_t *seventh, mutex_t *eighth, mutex_t *ninth,
    struct assertion_alias_state *required, int select)
{
	overflow_alias_wrapper(first, second, third, fourth, fifth, sixth,
	    seventh, eighth, ninth, required, select);
}

#else
#error "unsupported ASSERTION_ALIAS_VARIANT"
#endif
