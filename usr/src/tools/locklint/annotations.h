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
struct instruction;
struct position;
struct symbol;

enum locklint_protection {
	LOCKLINT_PROTECTION_NONE,
	LOCKLINT_PROTECTION_MUTEX,
	LOCKLINT_PROTECTION_RWLOCK,
	LOCKLINT_PROTECTION_SCHEME
};

struct locklint_data_policy {
	enum locklint_protection protection;
	bool readable_without_lock;
	bool read_only;
};

enum locklint_execution_kind {
	LOCKLINT_EXECUTION_NONE,
	LOCKLINT_EXECUTION_NO_COMPETITION,
	LOCKLINT_EXECUTION_COMPETITION,
	LOCKLINT_EXECUTION_INVISIBLE,
	LOCKLINT_EXECUTION_VISIBLE,
	LOCKLINT_EXECUTION_ASSUME_PROTECTED,
	LOCKLINT_EXECUTION_NO_COMPETITION_EFFECT,
	LOCKLINT_EXECUTION_COMPETITION_EFFECT
};

typedef void (*locklint_order_edge_f)(const struct locklint_access *,
    const char *, const struct locklint_access *, const char *,
    const struct position *, void *);

void locklint_annotations_enable(void);
bool locklint_data_policy(const struct locklint_access *,
    struct locklint_data_policy *, struct locklint_access *);
void locklint_for_each_order_edge(locklint_order_edge_f, void *);
enum locklint_execution_kind locklint_get_execution_annotation(
    const struct instruction *);
void locklint_resolve_annotations(void);
void locklint_show_annotations(FILE *);

#endif /* ANNOTATIONS_H */
