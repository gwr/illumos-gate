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
	unsigned int root_reasons;
	bool reachable_from_root;
};

#endif /* FUNCTION_INFO_H */
