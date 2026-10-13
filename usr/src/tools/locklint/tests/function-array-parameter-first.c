/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Force Sparse to apply function-parameter adjustment while checking an
 * indirect call through a shared-header operation vector.
 */

#include "function-array-parameter.h"

int
release_value(struct array_parameter_owner *owner, unsigned long *value)
{
	return (owner->ops->release(owner->handle, 1, value));
}
