/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify that a whole-object visibility marker covers member accesses when
 * the object's identity comes from an unresolved accessor return.  The first
 * write is private initialization; the second is a post-publication control.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

struct allocated_state {
	int value;
};

_NOTE(READ_ONLY_DATA(allocated_state::value))

extern struct allocated_state *get_state(void);

static void
initialize_allocated_state(void)
{
	struct allocated_state *state = get_state();

	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*state))
	state->value = 1;
	_NOTE(NOW_VISIBLE_TO_OTHER_THREADS(*state))
	state->value = 2;
}

static void
reassign_allocated_state(void)
{
	struct allocated_state *state = get_state();

	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(*state))
	state = get_state();
	state->value = 3;
}
