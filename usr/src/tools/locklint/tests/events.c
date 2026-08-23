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
 * This test covers ordered source events.  The fixture accesses protected
 * and unprotected members around `mutex_enter()` and `mutex_exit()`, and
 * includes a direct function call.
 */

/*
 * _NOTE discards an argument that intentionally contains non-C tokens.
 */
#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct event_state {
	int value;
	int other;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(event_state::lock, event_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void event_helper(struct event_state *);

static int
event_sequence(struct event_state *state)
{
	int value;

	value = state->value;
	value += state->other;
	mutex_enter(&state->lock);
	state->value = value;
	event_helper(state);
	mutex_exit(&state->lock);

	return (state->value);
}
