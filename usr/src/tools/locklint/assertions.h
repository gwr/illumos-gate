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

#ifndef ASSERTIONS_H
#define	ASSERTIONS_H

struct instruction;
struct locklint_access;
struct translation_unit;

enum locklint_assertion {
	LOCKLINT_ASSERT_NONE,
	LOCKLINT_ASSERT_HELD,
	LOCKLINT_ASSERT_NOT_HELD
};

void locklint_assertions_enable(void);
enum locklint_assertion locklint_get_assertion(struct translation_unit *,
    struct instruction *, struct locklint_access *);

#endif /* ASSERTIONS_H */
