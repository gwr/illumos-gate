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
 * Verify that command-file lock-order declarations share source LOCK_ORDER
 * name resolution and graph semantics.  The source declaration also permits
 * source/command duplicate and conflict coverage without acquisition noise.
 */

#define	_NOTE(arg)

typedef struct command_order_mutex {
	int opaque;
} command_order_mutex_t;

struct command_order_state {
	command_order_mutex_t first;
	command_order_mutex_t second;
	command_order_mutex_t third;
};

command_order_mutex_t command_order_global;
struct command_order_state command_order_object;

_NOTE(LOCK_ORDER(command_order_state::first
    command_order_state::second))

extern int mutex_lock(command_order_mutex_t *);
extern int mutex_unlock(command_order_mutex_t *);

static void
command_order_inversion(struct command_order_state *state)
{
	(void) mutex_lock(&state->third);
	(void) mutex_lock(&state->second);
	(void) mutex_unlock(&state->second);
	(void) mutex_unlock(&state->third);
}
