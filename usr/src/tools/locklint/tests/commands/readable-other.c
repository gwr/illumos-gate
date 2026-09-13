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
 * Verify that one type-member command applies to an equivalent type parsed
 * in another translation unit.
 */

#include "readable.h"

struct incomplete_command_state {
	command_mutex_t lock;
	int readable;
};

struct incomplete_command_state incomplete_command_object;

_NOTE(MUTEX_PROTECTS_DATA(incomplete_command_state::lock,
    incomplete_command_state::readable))

struct duplicate_command_type {
	int value;
};

int command_readable_incomplete(void);
int command_readable_ambiguous_b(struct duplicate_command_type *);

int
command_readable_other(command_state_t *state)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)

	value = state->readable;
	state->readable = value;
	return (value);
}

int
command_readable_incomplete(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)

	value = incomplete_command_object.readable;
	incomplete_command_object.readable = value;
	return (value);
}

int
command_readable_ambiguous_b(struct duplicate_command_type *state)
{
	return (state->value);
}
