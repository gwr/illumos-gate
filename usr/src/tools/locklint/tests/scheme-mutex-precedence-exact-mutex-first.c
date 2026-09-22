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
 * Apply mutex then scheme protection to the exact same member.
 */

#include "scheme-mutex-precedence.h"

_NOTE(MUTEX_PROTECTS_DATA(precedence_outer::lock,
    precedence_outer::inner.value))
_NOTE(SCHEME_PROTECTS_DATA("stable data", precedence_outer::inner.value))

void check_exact_mutex_first(struct precedence_outer *);

void
check_exact_mutex_first(struct precedence_outer *state)
{
	state->inner.value = 1;
}
