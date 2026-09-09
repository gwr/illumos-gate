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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Characterize declared lock-order checking.  The first three locks form a
 * chain, so taking them from left to right is valid while taking first after
 * second or third is a direct or transitive inversion.  The repeated case
 * first completes a valid held sequence, then proves that a later acquisition
 * of the same lock is checked against the state at that second sequence.
 *
 * The callee cases prove that an acquire-and-release operation must remain
 * visible to callers even though it has no net lock-state effect.  The mixed
 * case establishes that mutex and readers-writer lock roles share one order
 * graph.  The companion lock-order-cycle.c fixture isolates an invalid
 * declaration set, while LOCK_ORDER_COMMAS exercises locklint's optional
 * comma syntax.
 */

#ifdef __lock_lint
#include <sys/note.h>
#include <synch.h>
#else
#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;
#endif

struct order_state {
	mutex_t first;
	mutex_t second;
	mutex_t third;
};

#ifdef LOCK_ORDER_COMMAS
_NOTE(LOCK_ORDER(order_state::first, order_state::second,
    order_state::third))
#else
_NOTE(LOCK_ORDER(order_state::first order_state::second
    order_state::third))
#endif

struct mixed_order_state {
	mutex_t mutex;
	rwlock_t rwlock;
};

_NOTE(LOCK_ORDER(mixed_order_state::mutex mixed_order_state::rwlock))

#ifndef __lock_lint
extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);
extern int rw_rdlock(rwlock_t *);
extern int rw_unlock(rwlock_t *);
#endif

static void
order_valid(struct order_state *state)
{
	(void) mutex_lock(&state->first);
	(void) mutex_lock(&state->second);
	(void) mutex_unlock(&state->second);
	(void) mutex_unlock(&state->first);
}

static void
order_direct_inversion(struct order_state *state)
{
	(void) mutex_lock(&state->second);
	(void) mutex_lock(&state->first);
	(void) mutex_unlock(&state->first);
	(void) mutex_unlock(&state->second);
}

static void
order_transitive_inversion(struct order_state *state)
{
	(void) mutex_lock(&state->third);
	(void) mutex_lock(&state->first);
	(void) mutex_unlock(&state->first);
	(void) mutex_unlock(&state->third);
}

static void
order_repeated_sequences(struct order_state *state)
{
	(void) mutex_lock(&state->first);
	(void) mutex_lock(&state->second);
	(void) mutex_unlock(&state->second);
	(void) mutex_unlock(&state->first);

	(void) mutex_lock(&state->second);
	(void) mutex_lock(&state->first);
	(void) mutex_unlock(&state->first);
	(void) mutex_unlock(&state->second);
}

static void
acquire_first(struct order_state *state)
{
	(void) mutex_lock(&state->first);
	(void) mutex_unlock(&state->first);
}

static void
order_callee_inversion(struct order_state *state)
{
	(void) mutex_lock(&state->second);
	acquire_first(state);
	(void) mutex_unlock(&state->second);
}

static void
acquire_first_wrapper(struct order_state *state)
{
	acquire_first(state);
}

static void
order_wrapped_callee_inversion(struct order_state *state)
{
	(void) mutex_lock(&state->second);
	acquire_first_wrapper(state);
	(void) mutex_unlock(&state->second);
}

static void
mixed_order_valid(struct mixed_order_state *state)
{
	(void) mutex_lock(&state->mutex);
	(void) rw_rdlock(&state->rwlock);
	(void) rw_unlock(&state->rwlock);
	(void) mutex_unlock(&state->mutex);
}

static void
mixed_order_inversion(struct mixed_order_state *state)
{
	(void) rw_rdlock(&state->rwlock);
	(void) mutex_lock(&state->mutex);
	(void) mutex_unlock(&state->mutex);
	(void) rw_unlock(&state->rwlock);
}
