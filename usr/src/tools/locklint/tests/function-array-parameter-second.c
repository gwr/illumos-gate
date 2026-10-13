/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Retain a second exact definition of the operation vector after the first
 * translation unit has adjusted and called its array-form parameter.
 */

#include "function-array-parameter.h"

static struct array_parameter_owner second_owner;
