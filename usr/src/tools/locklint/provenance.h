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

#ifndef PROVENANCE_H
#define	PROVENANCE_H

#include <stdbool.h>
#include <stddef.h>

#include "context.h"

/*
 * An incoming edge records one caller context and call instruction which
 * reached the context that owns this edge.
 */
struct provenance_edge {
	struct function_context *caller_context;
	struct instruction *call_instruction;
	avl_node_t by_key;
};

typedef void (*provenance_root_call_f)(struct function_context *,
    struct instruction *, void *);

void provenance_edges_create(struct function_context *);
void provenance_edges_free(struct function_context *);

int provenance_edge_create(struct function_context *, struct function_context *,
    struct instruction *, struct provenance_edge **, bool *);

struct provenance_edge *provenance_edge_first(struct function_context *);
struct provenance_edge *provenance_edge_next(struct function_context *,
    struct provenance_edge *);
size_t provenance_edge_count(struct function_context *);

int provenance_for_each_root_call(struct function_context *,
    provenance_root_call_f, void *, bool *);

#endif /* PROVENANCE_H */
