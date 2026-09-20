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
#include "statistics.h"

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

struct context_visit {
	struct function_context *context;
	struct context_visit *next;
	avl_node_t by_context;
};

struct root_call {
	struct function_context *context;
	struct instruction *instruction;
	avl_node_t by_call;
};

static int
compare_context_visits(const void *left_arg, const void *right_arg)
{
	const struct context_visit *left = left_arg;
	const struct context_visit *right = right_arg;

	return (AVL_PCMP(left->context, right->context));
}

static int
compare_root_calls(const void *left_arg, const void *right_arg)
{
	const struct root_call *left = left_arg;
	const struct root_call *right = right_arg;
	int result;

	result = AVL_PCMP(left->context, right->context);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->instruction, right->instruction));
}

static int
record_context_visit(avl_tree_t *visited, struct context_visit **work,
    struct function_context *context)
{
	struct context_visit key = {
		.context = context
	};
	struct context_visit *visit;
	avl_index_t where;

	visit = avl_find(visited, &key, &where);
	if (visit != NULL)
		return (0);
	visit = calloc(1, sizeof (*visit));
	if (visit == NULL)
		return (ENOMEM);
	visit->context = context;
	visit->next = *work;
	*work = visit;
	avl_insert(visited, visit, where);
	return (0);
}

static int
record_root_call(avl_tree_t *calls, struct function_context *context,
    struct instruction *instruction)
{
	struct root_call key = {
		.context = context,
		.instruction = instruction
	};
	struct root_call *call;
	avl_index_t where;

	call = avl_find(calls, &key, &where);
	if (call != NULL)
		return (0);
	call = calloc(1, sizeof (*call));
	if (call == NULL)
		return (ENOMEM);
	*call = key;
	avl_insert(calls, call, where);
	return (0);
}

static void
free_context_visits(avl_tree_t *visited)
{
	struct context_visit *visit;
	void *cookie = NULL;

	while ((visit = avl_destroy_nodes(visited, &cookie)) != NULL)
		free(visit);
	avl_destroy(visited);
}

static void
free_root_calls(avl_tree_t *calls)
{
	struct root_call *call;
	void *cookie = NULL;

	while ((call = avl_destroy_nodes(calls, &cookie)) != NULL)
		free(call);
	avl_destroy(calls);
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

	statistics.cleanup_provenance_edges_enum++;
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

/*
 * Visit each distinct call made by a synthetic root which can reach a
 * concrete context.  Context identity bounds recursive cycles, while the
 * separate call set removes duplicates introduced by converging paths.
 * Allocation is completed before callbacks begin.
 */
int
provenance_for_each_root_call(struct function_context *context,
    provenance_root_call_f callback, void *data, bool *found)
{
	struct context_visit *work = NULL;
	struct root_call *call;
	avl_tree_t visited;
	avl_tree_t calls;
	int error;

	avl_create(&visited, compare_context_visits,
	    sizeof (struct context_visit),
	    offsetof(struct context_visit, by_context));
	avl_create(&calls, compare_root_calls, sizeof (struct root_call),
	    offsetof(struct root_call, by_call));
	error = record_context_visit(&visited, &work, context);
	while (error == 0 && work != NULL) {
		struct context_visit *visit = work;
		struct provenance_edge *edge;

		work = visit->next;
		statistics.caller_recovery_provenance_edges_enum++;
		for (edge = provenance_edge_first(visit->context); edge != NULL;
		    edge = provenance_edge_next(visit->context, edge)) {
			if (edge->caller_context->kind ==
			    FUNCTION_CONTEXT_ROOT) {
				error = record_root_call(&calls,
				    edge->caller_context, edge->call_instruction);
			} else {
				error = record_context_visit(&visited, &work,
				    edge->caller_context);
			}
			if (error != 0)
				break;
		}
	}
	free_context_visits(&visited);
	if (error != 0) {
		free_root_calls(&calls);
		return (error);
	}
	*found = !avl_is_empty(&calls);
	for (call = avl_first(&calls); call != NULL;
	    call = AVL_NEXT(&calls, call))
		callback(call->context, call->instruction, data);
	free_root_calls(&calls);
	return (0);
}
