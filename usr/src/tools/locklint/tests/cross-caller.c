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
 * See cross.h
 */

#include "cross.h"

int
cross_locked(struct cross_state *state)
{
	int value;

	mutex_enter(&state->lock);
	value = cross_read(state);
	mutex_exit(&state->lock);
	return (value);
}

int
cross_unlocked(struct cross_state *state)
{
	return (cross_read(state));
}

int
cross_effect(struct cross_state *state)
{
	int value;

	cross_acquire(state);
	value = cross_read(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
cross_private_helper(int value)
{
	return (value);
}

int
cross_caller_private_path(int value)
{
	return (cross_private_helper(value));
}
