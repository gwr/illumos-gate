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

#ifndef LOCK_IDENTITY_H
#define	LOCK_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "avl.h"

/*
 * analysis_object is an opaque identity token.  target_offset is compared as
 * a separate coordinate and must never be applied to or encoded in the
 * pointer.
 */
struct lock_identity_key {
	const void *analysis_object;
	int64_t target_offset;
};

/*
 * This non-key description records how to interpret analysis_object.  It
 * never participates in identity or ordering.
 */
enum lock_analysis_object_type {
	LOCK_ANALYSIS_OBJECT_UNSPECIFIED,
	LOCK_ANALYSIS_OBJECT_OBJECT_IDENTITY,
	LOCK_ANALYSIS_OBJECT_SYMBOL,
	LOCK_ANALYSIS_OBJECT_PSEUDO
};

struct lock_identity {
	struct lock_identity_key key;
	enum lock_analysis_object_type analysis_object_type;
	avl_node_t by_key;
};

struct lock_identity_collection {
	avl_tree_t identities;
};

void lock_identity_collection_create(struct lock_identity_collection *);
void lock_identity_collection_free(struct lock_identity_collection *);

int lock_identity_intern(struct lock_identity_collection *,
    struct lock_identity_key, enum lock_analysis_object_type,
    struct lock_identity **, bool *);
struct lock_identity *lock_identity_first(struct lock_identity_collection *);
struct lock_identity *lock_identity_next(struct lock_identity_collection *,
    struct lock_identity *);
size_t lock_identity_count(struct lock_identity_collection *);

#endif /* LOCK_IDENTITY_H */
