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

#ifndef BINDING_H
#define	BINDING_H

#include <stdbool.h>
#include <stddef.h>

#include "avl.h"
#include "lock_identity.h"

struct call_target_set;

/*
 * A binding maps one zero-based formal argument number to the canonical
 * identity of its actual object in the caller.  Environments are immutable,
 * sorted by argument number, and interned within the callee function.
 */
struct formal_binding {
	unsigned int argument;
	const struct lock_identity *actual_identity;
};

/*
 * A derived binding maps one exact callee-side expression and byte offset to
 * an existing canonical identity in the caller.
 */
struct derived_binding {
	struct lock_identity_key source;
	const struct lock_identity *actual_identity;
};

struct formal_target_binding {
	unsigned int argument;
	const struct call_target_set *actual_targets;
};

struct binding_environment {
	avl_node_t by_value;
	size_t count;
	size_t derived_count;
	size_t target_count;
	struct derived_binding *derived_entries;
	struct formal_target_binding *target_entries;
	struct formal_binding entries[];
};

struct binding_environment_collection {
	avl_tree_t environments;
};

void binding_collection_create(struct binding_environment_collection *);
void binding_collection_free(struct binding_environment_collection *);

int binding_environment_intern(struct binding_environment_collection *,
    const struct formal_binding *, size_t, const struct derived_binding *,
    size_t, struct binding_environment **, bool *);
int binding_environment_intern_targets(struct binding_environment_collection *,
    const struct formal_binding *, size_t, const struct derived_binding *,
    size_t, const struct formal_target_binding *, size_t,
    struct binding_environment **, bool *);
const struct lock_identity *binding_environment_lookup(
    const struct binding_environment *, unsigned int);
const struct lock_identity *binding_environment_lookup_derived(
    const struct binding_environment *, struct lock_identity_key);
const struct call_target_set *binding_environment_lookup_targets(
    const struct binding_environment *, unsigned int);
size_t binding_environment_count(struct binding_environment_collection *);
size_t binding_environment_entry_count(
    struct binding_environment_collection *);

#endif /* BINDING_H */
