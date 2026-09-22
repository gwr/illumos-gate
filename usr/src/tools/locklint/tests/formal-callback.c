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
 * Verify context-sensitive propagation of exact function targets through a
 * formal function-pointer parameter.  Each caller holds a distinct outer
 * lock so combining the two callers' target sets would produce extra wait
 * diagnostics.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct condvar {
	int opaque;
} condvar_t;

struct formal_callback_state {
	mutex_t outer_first;
	mutex_t outer_second;
	mutex_t wait_first;
	mutex_t wait_second;
	condvar_t cv_first;
	condvar_t cv_second;
};

typedef void (*wait_callback_t)(struct formal_callback_state *);

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void cv_wait(condvar_t *, mutex_t *);

static void
wait_first(struct formal_callback_state *state)
{
	mutex_enter(&state->wait_first);
	cv_wait(&state->cv_first, &state->wait_first);
	mutex_exit(&state->wait_first);
}

static void
wait_second(struct formal_callback_state *state)
{
	mutex_enter(&state->wait_second);
	cv_wait(&state->cv_second, &state->wait_second);
	mutex_exit(&state->wait_second);
}

static void
invoke_callback(struct formal_callback_state *state, wait_callback_t callback)
{
	callback(state);
}

static void
forward_callback(struct formal_callback_state *state, wait_callback_t callback)
{
	invoke_callback(state, callback);
}

static void
call_first(struct formal_callback_state *state)
{
	mutex_enter(&state->outer_first);
	forward_callback(state, wait_first);
	mutex_exit(&state->outer_first);
}

static void
call_second(struct formal_callback_state *state)
{
	mutex_enter(&state->outer_second);
	forward_callback(state, wait_second);
	mutex_exit(&state->outer_second);
}
