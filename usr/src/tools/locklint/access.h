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

#ifndef ACCESS_H
#define	ACCESS_H

#include <stdbool.h>
#include <stdio.h>

struct expression;
struct symbol;

struct locklint_access {
	struct symbol *root;
	struct symbol *type;
	struct symbol *member;
	unsigned long offset;
	struct expression *expr;
};

bool locklint_get_access(struct expression *, struct locklint_access *);
bool locklint_access_base(const struct locklint_access *, struct symbol *,
    unsigned long, unsigned long *);
void locklint_show_access(FILE *, struct expression *);

#endif /* ACCESS_H */
