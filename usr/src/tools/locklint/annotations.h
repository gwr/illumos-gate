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

#ifndef ANNOTATIONS_H
#define	ANNOTATIONS_H

#include <stdbool.h>
#include <stdio.h>

struct locklint_access;
struct symbol;

enum locklint_protection {
	LOCKLINT_PROTECTION_NONE,
	LOCKLINT_PROTECTION_MUTEX,
	LOCKLINT_PROTECTION_SCHEME
};

struct locklint_data_policy {
	enum locklint_protection protection;
	bool readable_without_lock;
	bool read_only;
};

void locklint_annotations_enable(void);
bool locklint_data_policy(const struct locklint_access *,
    struct locklint_data_policy *, struct locklint_access *);
void locklint_resolve_annotations(void);
void locklint_show_annotations(FILE *);

#endif /* ANNOTATIONS_H */
