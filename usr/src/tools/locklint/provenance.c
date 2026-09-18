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

void
provenance_fini(struct function_context *context)
{
	while (context->provenance_edges != NULL) {
		struct provenance_edge *next = context->provenance_edges->next;

		free(context->provenance_edges);
		context->provenance_edges = next;
	}
}

/*
 * Find or create an incoming edge.  Allocation failure leaves both output
 * arguments and the context unchanged.
 */
int
provenance_edge_get(struct function_context *callee_context,
    struct function_context *caller_context,
    struct instruction *call_instruction, struct provenance_edge **result,
    bool *created)
{
	struct provenance_edge *edge;

	for (edge = callee_context->provenance_edges; edge != NULL;
	    edge = edge->next) {
		if (edge->caller_context == caller_context &&
		    edge->call_instruction == call_instruction) {
			*result = edge;
			*created = false;
			return (0);
		}
	}
	edge = calloc(1, sizeof (*edge));
	if (edge == NULL)
		return (ENOMEM);
	edge->caller_context = caller_context;
	edge->call_instruction = call_instruction;
	edge->next = callee_context->provenance_edges;
	callee_context->provenance_edges = edge;
	*result = edge;
	*created = true;
	return (0);
}

size_t
provenance_edge_count(const struct function_context *context)
{
	const struct provenance_edge *edge;
	size_t count = 0;

	for (edge = context->provenance_edges; edge != NULL; edge = edge->next)
		count++;
	return (count);
}
