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
 * Place the containing mutex policy, nested scheme policy, and access in one
 * translation unit as a control for the separate-database cases.
 */

#include "scheme-mutex-precedence.h"

_NOTE(MUTEX_PROTECTS_DATA(precedence_outer::lock, precedence_outer))
_NOTE(SCHEME_PROTECTS_DATA("stable data", precedence_inner))

void check_scheme_mutex_precedence(struct precedence_outer *);

void
check_scheme_mutex_precedence(struct precedence_outer *state)
{
	state->inner.value = 1;
}
