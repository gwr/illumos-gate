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
 * Verify type-scoped external-scheme protection declared by command.
 * Source declarations provide duplicate and mechanical conflict cases.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct command_scheme_state {
	mutex_t lock;
	int value;
	int duplicate;
	int source_value;
	int mechanical;
};

_NOTE(SCHEME_PROTECTS_DATA("source scheme",
    command_scheme_state::source_value))

_NOTE(MUTEX_PROTECTS_DATA(command_scheme_state::lock,
    command_scheme_state::mechanical))

int
command_scheme_access(struct command_scheme_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)

	state->value++;
	state->duplicate++;
	state->source_value++;
	return (state->value + state->duplicate + state->source_value);
}
