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
 * Copyright 2026 RackTop Systems Inc.
 */

/*
 * Test annotation names, generated paths, recursive structure expansion,
 * and replacement of earlier protection declarations.
 */

#ifdef __lock_lint
#include <sys/note.h>
typedef struct mutex {
	int opaque;
} mutex_t;
#else
#define	_NOTE(arg)
typedef int mutex_t;
#endif

typedef struct inner_state {
	int first;
	int second;
} inner_state_t;

typedef struct generated_state {
	mutex_t lock;
	inner_state_t nested;
} generated_state_t;

typedef struct recursive_state {
	mutex_t lock;
	int direct;
	inner_state_t nested;
	union {
		int anonymous_value;
	};
} recursive_state_t;

typedef struct override_state {
	mutex_t first_lock;
	mutex_t last_lock;
	int value;
} override_state_t;

static mutex_t global_lock;
static int global_value;
static recursive_state_t global_state;

_NOTE(MUTEX_PROTECTS_DATA(global_lock, global_value))
_NOTE(MUTEX_PROTECTS_DATA(global_state.lock, global_state.direct))
_NOTE(MUTEX_PROTECTS_DATA(generated_state::lock,
    generated_state::{ nested.{ first second } }))
_NOTE(MUTEX_PROTECTS_DATA(recursive_state::lock, recursive_state))
_NOTE(MUTEX_PROTECTS_DATA(override_state::first_lock,
    override_state::value))
_NOTE(MUTEX_PROTECTS_DATA(override_state::last_lock,
    override_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
check_global_names(void)
{
	global_value = 1;
	mutex_enter(&global_lock);
	global_value = 2;
	mutex_exit(&global_lock);

	global_state.direct = 1;
	mutex_enter(&global_state.lock);
	global_state.direct = 2;
	mutex_exit(&global_state.lock);
}

static void
check_generated_names(generated_state_t *state)
{
	state->nested.first = 1;
	state->nested.second = 1;
	mutex_enter(&state->lock);
	state->nested.first = 2;
	state->nested.second = 2;
	mutex_exit(&state->lock);
}

static void
check_recursive_names(recursive_state_t *state)
{
	state->direct = 1;
	state->nested.first = 1;
	state->nested.second = 1;
	state->anonymous_value = 1;
	mutex_enter(&state->lock);
	state->direct = 2;
	state->nested.first = 2;
	state->nested.second = 2;
	state->anonymous_value = 2;
	mutex_exit(&state->lock);
}

static void
check_override(override_state_t *state)
{
	mutex_enter(&state->first_lock);
	state->value = 1;
	mutex_exit(&state->first_lock);

	mutex_enter(&state->last_lock);
	state->value = 2;
	mutex_exit(&state->last_lock);
}

typedef struct wrapper_state {
	int padding;
	recursive_state_t state;
} wrapper_state_t;

static void
check_embedded_type(wrapper_state_t *wrapper)
{
	wrapper->state.nested.first = 1;
	mutex_enter(&wrapper->state.lock);
	wrapper->state.nested.first = 2;
	mutex_exit(&wrapper->state.lock);
}

typedef struct global_data {
	int value;
} global_data_t;

typedef struct global_wrapper {
	int padding;
	global_data_t data;
} global_wrapper_t;

_NOTE(MUTEX_PROTECTS_DATA(global_lock, global_data::value))

static void
check_embedded_global_lock(global_wrapper_t *wrapper)
{
	wrapper->data.value = 1;
	mutex_enter(&global_lock);
	wrapper->data.value = 2;
	mutex_exit(&global_lock);
}
