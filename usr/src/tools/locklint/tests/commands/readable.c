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
 * Verify that command-file readable policy applies to a type member and an
 * external object while leaving writes protected.
 */

#include "readable.h"

struct incomplete_command_state;

enum {
	COMMAND_READABLE_ENUM
};

command_state_t command_object;
command_mutex_t command_global_lock;
int command_global;
extern struct incomplete_command_state incomplete_command_object;

struct duplicate_command_type {
	int value;
};

int command_readable_ambiguous_a(struct duplicate_command_type *);

int
command_readable_primary(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)

	value = command_object.readable;
	command_object.readable = value;
	value += command_object.readable_group.first;
	command_object.readable_group.second = value;
	value += command_global;
	command_global = value;
	return (value);
}

int
command_readable_ambiguous_a(struct duplicate_command_type *state)
{
	return (state->value);
}
