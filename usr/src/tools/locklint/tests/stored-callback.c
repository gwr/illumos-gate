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
 * Verify path-sensitive propagation of exact callback targets through a
 * stored function-pointer member within one function.  Distinct callers use
 * distinct callbacks and outer locks so a combined target set would produce
 * extra wait diagnostics.  An unknown replacement must invalidate the exact
 * target.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct condvar {
	int opaque;
} condvar_t;

struct stored_callback_state {
	mutex_t outer_first;
	mutex_t outer_second;
	mutex_t wait_first;
	mutex_t wait_second;
	condvar_t cv_first;
	condvar_t cv_second;
};

typedef void (*stored_callback_t)(struct stored_callback_state *);

struct stored_request {
	struct stored_callback_state *state;
	stored_callback_t volatile callback;
};

static struct stored_request request;

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void cv_wait(condvar_t *, mutex_t *);
extern stored_callback_t unknown_callback(void);

static void
wait_first(struct stored_callback_state *state)
{
	mutex_enter(&state->wait_first);
	cv_wait(&state->cv_first, &state->wait_first);
	mutex_exit(&state->wait_first);
}

static void
wait_second(struct stored_callback_state *state)
{
	mutex_enter(&state->wait_second);
	cv_wait(&state->cv_second, &state->wait_second);
	mutex_exit(&state->wait_second);
}

static void
unknown_side_effect(void)
{
}

static void
call_first(struct stored_callback_state *state)
{
	mutex_enter(&state->outer_first);
	request.state = state;
	request.callback = wait_first;
	request.callback(request.state);
	mutex_exit(&state->outer_first);
}

static void
call_second(struct stored_callback_state *state)
{
	mutex_enter(&state->outer_second);
	request.state = state;
	request.callback = wait_second;
	request.callback(request.state);
	mutex_exit(&state->outer_second);
}

static void
call_invalidated(struct stored_callback_state *state)
{
	mutex_enter(&state->outer_first);
	request.state = state;
	request.callback = wait_first;
	request.callback = unknown_callback();
	request.callback(request.state);
	mutex_exit(&state->outer_first);
}

static void
call_cleared(struct stored_callback_state *state)
{
	mutex_enter(&state->outer_second);
	request.state = state;
	request.callback = wait_first;
	unknown_side_effect();
	request.callback(request.state);
	mutex_exit(&state->outer_second);
}
