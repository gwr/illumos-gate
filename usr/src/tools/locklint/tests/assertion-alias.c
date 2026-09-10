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

#else
#error "unsupported ASSERTION_ALIAS_VARIANT"
#endif
