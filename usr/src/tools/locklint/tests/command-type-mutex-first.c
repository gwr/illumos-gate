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
 * Exercise the first equivalent type origin and provide a source declaration
 * for source/command duplicate and conflict tests.
 */

#define	_NOTE(arg)

#include "command-type-mutex-first.h"

command_type_state_t command_type_object;

_NOTE(MUTEX_PROTECTS_DATA(command_type_state::lock,
    command_type_state::source_value))

extern void mutex_enter(command_type_mutex_t *);
extern void mutex_exit(command_type_mutex_t *);

int
command_type_mutex_first(command_type_state_t *state)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)

	value = state->value;
	value += state->nested.first;
	value += state->nested.second;
	mutex_enter(&state->lock);
	value += state->value;
	value += state->nested.first;
	value += state->nested.second;
	mutex_exit(&state->lock);
	return (value);
}
