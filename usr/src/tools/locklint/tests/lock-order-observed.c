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
 * Verify deadlock detection from observed acquisitions without LOCK_ORDER
 * declarations.  One direction crosses a direct call and the other crosses a
 * wrapper, so together they form a cycle only after interprocedural
 * acquisitions are combined module-wide.  Each function remains balanced and
 * locally consistent.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct observed_order_state {
	mutex_t first;
	mutex_t second;
};

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
observed_acquire_second(struct observed_order_state *state)
{
	mutex_enter(&state->second);
	mutex_exit(&state->second);
}

static void
observed_first_then_second(struct observed_order_state *state)
{
	mutex_enter(&state->first);
	observed_acquire_second(state);
	mutex_exit(&state->first);
}

static void
observed_acquire_first(struct observed_order_state *state)
{
	mutex_enter(&state->first);
	mutex_exit(&state->first);
}

static void
observed_acquire_first_wrapper(struct observed_order_state *state)
{
	observed_acquire_first(state);
}

static void
observed_second_then_first(struct observed_order_state *state)
{
	mutex_enter(&state->second);
	observed_acquire_first_wrapper(state);
	mutex_exit(&state->second);
}
