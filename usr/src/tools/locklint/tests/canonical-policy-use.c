/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Exercise the corresponding exact type from a different translation unit.
 */

#include "canonical-policy.h"

int
canonical_policy_use(struct canonical_policy_state *state)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)

	value = state->value;
	state->value = value;

	mutex_enter(&state->lock);
	state->value = value;
	mutex_exit(&state->lock);

	return (value);
}
