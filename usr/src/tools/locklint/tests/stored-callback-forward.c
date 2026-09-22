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
 * Verify that an exact stored callback demand propagates through a helper
 * which forwards the callback-bearing pointer to the helper that loads it.
 * Distinct callers must retain distinct targets without importing an
 * unrelated request.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct condvar {
	int opaque;
} condvar_t;

struct stored_callback_forward_state {
	mutex_t outer_first;
	mutex_t outer_second;
	mutex_t wait_first;
	mutex_t wait_second;
	condvar_t cv_first;
	condvar_t cv_second;
};

typedef void (*stored_callback_t)(struct stored_callback_forward_state *);

struct stored_request {
	struct stored_callback_forward_state *state;
	stored_callback_t volatile callback;
};

static struct stored_request first_request;
static struct stored_request second_request;
static struct stored_request unrelated_request;

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void cv_wait(condvar_t *, mutex_t *);

static void
wait_first(struct stored_callback_forward_state *state)
{
	mutex_enter(&state->wait_first);
	cv_wait(&state->cv_first, &state->wait_first);
	mutex_exit(&state->wait_first);
}

static void
wait_second(struct stored_callback_forward_state *state)
{
	mutex_enter(&state->wait_second);
	cv_wait(&state->cv_second, &state->wait_second);
	mutex_exit(&state->wait_second);
}

static void
invoke_stored(struct stored_request *request)
{
	request->callback(request->state);
}

static void
forward_stored(struct stored_request *request)
{
	invoke_stored(request);
}

static void
call_first(struct stored_callback_forward_state *state)
{
	mutex_enter(&state->outer_first);
	first_request.state = state;
	first_request.callback = wait_first;
	unrelated_request.state = state;
	unrelated_request.callback = wait_second;
	forward_stored(&first_request);
	mutex_exit(&state->outer_first);
}

static void
call_second(struct stored_callback_forward_state *state)
{
	mutex_enter(&state->outer_second);
	second_request.state = state;
	second_request.callback = wait_second;
	unrelated_request.state = state;
	unrelated_request.callback = wait_first;
	forward_stored(&second_request);
	mutex_exit(&state->outer_second);
}
