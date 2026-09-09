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

struct assumed_region;
struct acquisition_summary;
struct block_info;
struct entrypoint;
struct lock_transfer;
struct protection_condition;
struct translation_unit;
struct visibility_transfer;

/*
 * Shared semantic state for one function.  Callgraph indexing and collection
 * linkage are intentionally private to callgraph.c.
 */
struct function_info {
	/* Functions and their retained Sparse objects belong to one parse. */
	struct translation_unit *tu;
	struct entrypoint *ep;
	struct block_info *blocks;
	struct protection_condition *conditions;
	struct assumed_region *assumptions;
	struct acquisition_summary *acquisitions;
	struct lock_transfer *transfers;
	struct visibility_transfer *visibility_transfers;
	unsigned int root_reasons;
	bool reachable_from_root;
};

#endif /* FUNCTION_INFO_H */
