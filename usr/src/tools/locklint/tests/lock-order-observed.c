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
 * declarations.  The first function contributes first-before-second and the
 * second contributes second-before-first, so together they form a cycle.
 * Each function alone is balanced and locally consistent; the conflict is
 * visible only after combining the module-wide observations.
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
observed_first_then_second(struct observed_order_state *state)
{
	mutex_enter(&state->first);
	mutex_enter(&state->second);
	mutex_exit(&state->second);
	mutex_exit(&state->first);
}

static void
observed_second_then_first(struct observed_order_state *state)
{
	mutex_enter(&state->second);
	mutex_enter(&state->first);
	mutex_exit(&state->first);
	mutex_exit(&state->second);
}
