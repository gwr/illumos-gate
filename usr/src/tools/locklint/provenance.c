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

/*
 * Maintain the incoming caller and call-site edges owned by each function
 * context.  These edges describe provenance but do not participate in
 * semantic context identity.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "context.h"
#include "provenance.h"

static int
compare_edges(const void *left_arg, const void *right_arg)
{
	const struct provenance_edge *left = left_arg;
	const struct provenance_edge *right = right_arg;
	int result;

	result = AVL_PCMP(left->caller_context, right->caller_context);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->call_instruction, right->call_instruction));
}

void
provenance_edges_create(struct function_context *context)
{
	avl_create(&context->provenance_edges, compare_edges,
	    sizeof (struct provenance_edge),
	    offsetof(struct provenance_edge, by_key));
}

void
provenance_edges_free(struct function_context *context)
{
	struct provenance_edge *edge;
	void *cookie = NULL;

	while ((edge = avl_destroy_nodes(&context->provenance_edges,
	    &cookie)) != NULL)
		free(edge);
	avl_destroy(&context->provenance_edges);
}

/*
 * Find or create an incoming edge.  Allocation failure leaves both output
 * arguments and the context unchanged.
 */
int
provenance_edge_create(struct function_context *callee_context,
    struct function_context *caller_context,
    struct instruction *call_instruction, struct provenance_edge **result,
    bool *existed)
{
	struct provenance_edge key = {
		.caller_context = caller_context,
		.call_instruction = call_instruction
	};
	struct provenance_edge *edge;
	avl_index_t where;

	edge = avl_find(&callee_context->provenance_edges, &key, &where);
	if (edge != NULL) {
		*result = edge;
		*existed = true;
		return (0);
	}
	edge = calloc(1, sizeof (*edge));
	if (edge == NULL)
		return (ENOMEM);
	edge->caller_context = caller_context;
	edge->call_instruction = call_instruction;
	avl_insert(&callee_context->provenance_edges, edge, where);
	*result = edge;
	*existed = false;
	return (0);
}

struct provenance_edge *
provenance_edge_first(struct function_context *context)
{
	return (avl_first(&context->provenance_edges));
}

struct provenance_edge *
provenance_edge_next(struct function_context *context,
    struct provenance_edge *edge)
{
	return (AVL_NEXT(&context->provenance_edges, edge));
}

size_t
provenance_edge_count(struct function_context *context)
{
	return (avl_numnodes(&context->provenance_edges));
}
