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
 * Compare direct, transitive, and recursive caller lock requirements.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct calls_basic_state {
	mutex_t lock;
	int direct_value;
	int transitive_value;
	int recursive_value;
} calls_basic_state_t;

_NOTE(MUTEX_PROTECTS_DATA(calls_basic_state::lock,
    calls_basic_state::{ direct_value transitive_value recursive_value }))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
read_direct(calls_basic_state_t *state)
{
	return (state->direct_value);
}

static int
read_transitive(calls_basic_state_t *state)
{
	return (state->transitive_value);
}

static int
read_wrapper(calls_basic_state_t *state)
{
	return (read_transitive(state));
}

static int
read_recursive(calls_basic_state_t *state, int depth)
{
	if (depth != 0)
		return (read_recursive(state, depth - 1));
	return (state->recursive_value);
}

static int
locked_direct(calls_basic_state_t *state)
{
	int value;

	mutex_enter(&state->lock);
	value = read_direct(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
unlocked_direct(calls_basic_state_t *state)
{
	return (read_direct(state));
}

static int
locked_transitive(calls_basic_state_t *state)
{
	int value;

	mutex_enter(&state->lock);
	value = read_wrapper(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
unlocked_transitive(calls_basic_state_t *state)
{
	return (read_wrapper(state));
}

static int
locked_recursive(calls_basic_state_t *state)
{
	int value;

	mutex_enter(&state->lock);
	value = read_recursive(state, 2);
	mutex_exit(&state->lock);
	return (value);
}

static int
unlocked_recursive(calls_basic_state_t *state)
{
	return (read_recursive(state, 2));
}
