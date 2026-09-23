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

#ifndef FUNCTION_INFO_H
#define	FUNCTION_INFO_H

#include <stdbool.h>
#include <stddef.h>

#include "avl.h"
#include "binding.h"
#include "context.h"
#include "lock_identity.h"

struct entrypoint;
struct translation_unit;

/*
 * One function-wide ASSUMING_PROTECTED entry contract.  The selected region
 * and optional mutex use formal-relative or canonical absolute coordinates,
 * so resolved calls can map them without retaining a flow-state dependency.
 */
struct assumed_region {
	struct visibility_region region;
	enum lock_analysis_object_type object_type;
	struct lock_identity_key mutex;
	enum lock_analysis_object_type mutex_object_type;
	const struct instruction *marker;
	char *name;
	char *mutex_name;
	bool valid;
	bool has_mutex;
	struct assumed_region *next;
};

/*
 * One exact function-pointer load rooted in a pointer formal.  These demands
 * are collected once per function so calls can later project only stored
 * targets which the callee actually observes.
 */
struct stored_target_demand {
	int64_t target_offset;
	uint64_t target_length;
	unsigned int argument;
};

/*
 * One exact callback assignment in a coherently initialized operation family.
 * Offsets are relative to the object returned by the setup function.
 */
struct operation_family_entry {
	avl_node_t by_region;
	int64_t target_offset;
	uint64_t target_length;
	const struct call_target_set *targets;
};

/*
 * One setup function's coherent operation-family assignment set.
 * Profiles and their entries are immutable once inserted into their owning
 * collections, so both AVL keys remain stable.
 */
struct operation_family_profile {
	avl_node_t by_content;
	avl_tree_t entries;
	size_t count;
};

/*
 * Shared semantic state for one function.  Callgraph indexing and collection
 * linkage are intentionally private to callgraph.c.
 */
struct function_info {
	/* Functions and their retained Sparse objects belong to one parse. */
	struct translation_unit *tu;
	struct entrypoint *ep;
	struct binding_environment_collection bindings;
	struct function_context_collection contexts;
	struct assumed_region *assumed_regions;
	struct assumed_region **assumed_regions_tail;
	struct lock_identity_key *derived_protectors;
	size_t derived_protector_count;
	size_t derived_protector_capacity;
	struct stored_target_demand *stored_target_demands;
	size_t stored_target_demand_count;
	size_t stored_target_demand_capacity;
	avl_tree_t operation_family_profiles;
	size_t operation_family_profile_count;
	unsigned int root_reasons;
	bool reachable_from_root;
	bool entry_no_competing_threads;
};

#endif /* FUNCTION_INFO_H */
