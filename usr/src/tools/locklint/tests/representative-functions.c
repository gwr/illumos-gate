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
 * This file is input only to locklint.  It supplies representative functions
 * for command-file contract tests and is not compiled or linked into a
 * product.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct condvar {
	int opaque;
} condvar_t;

struct command_target_state {
	mutex_t first;
	mutex_t second;
	mutex_t wait;
	condvar_t cv;
};

void command_target_representative(struct command_target_state *);

void
command_target_representative(struct command_target_state *state)
{
	(void) state;
}
