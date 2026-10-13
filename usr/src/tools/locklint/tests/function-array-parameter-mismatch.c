/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Preserve a genuine mismatch below the function-parameter boundary:
 * pointer-to-pointer and pointer-to-array remain different types.
 */

#define	NESTED_PARAMETER_POINTER
#include "function-array-parameter.h"

static struct array_parameter_owner mismatched_owner;
