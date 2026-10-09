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
 * Define an aggregate scheme in a shared header so repeated translation-unit
 * instances must expand and deduplicate to the same canonical member policy.
 */

#ifndef _DUPLICATE_AGGREGATE_SCHEME_H
#define	_DUPLICATE_AGGREGATE_SCHEME_H

#define	_NOTE(arg)	__attribute__((__lock_lint_##arg))

struct duplicate_scheme_state {
	int value;
};

extern struct duplicate_scheme_state *duplicate_scheme_state;

_NOTE(SCHEME_PROTECTS_DATA("shared header scheme",
    duplicate_scheme_state))

extern struct duplicate_scheme_state duplicate_scheme_shared;
extern int duplicate_scheme_read(void);

#endif
