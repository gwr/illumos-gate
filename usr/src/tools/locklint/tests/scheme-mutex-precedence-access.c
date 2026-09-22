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
 * Declare containing-structure mutex protection and perform an unlocked
 * write to a nested member.
 */

#include "scheme-mutex-precedence.h"

_NOTE(MUTEX_PROTECTS_DATA(precedence_outer::lock, precedence_outer))

void check_scheme_mutex_precedence(struct precedence_outer *);

void
check_scheme_mutex_precedence(struct precedence_outer *state)
{
	state->inner.value = 1;
}
