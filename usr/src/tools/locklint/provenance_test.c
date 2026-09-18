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
 * Exercise owner-local provenance edges independently of CFG traversal and
 * verify that call paths do not alter semantic function-context identity.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "context.h"
#include "dependency.h"
#include "function_info.h"
#include "provenance.h"

static unsigned int failures;

void
dependency_records_free(struct function_context *context)
{
	(void) context;
}

static void
check(bool condition, const char *message)
{
	if (condition)
		return;
	(void) fprintf(stderr, "FAIL: %s\n", message);
	failures++;
}

static int
compare_edges(const struct provenance_edge *left,
    const struct provenance_edge *right)
{
	int result;

	result = AVL_PCMP(left->caller_context, right->caller_context);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->call_instruction, right->call_instruction));
}

static struct function_context *
make_context(struct function_info *function)
{
	struct semantic_state *state = NULL;
	struct function_context *context = NULL;
	bool existed;
	int error;

	context_collection_create(function);
	error = context_empty_state_intern(function, &state, &existed);
	check(error == 0 && !existed, "create semantic state");
	error = context_create(function, NULL, state, &context, &existed);
	check(error == 0 && !existed, "create function context");
	return (context);
}

static void
test_provenance_edges(void)
{
	struct function_info callee_function = { 0 };
	struct function_info first_function = { 0 };
	struct function_info second_function = { 0 };
	struct function_context *callee;
	struct function_context *first;
	struct function_context *second;
	struct function_context *same_context;
	struct provenance_edge *first_edge;
	struct provenance_edge *same_edge;
	struct provenance_edge *other_site_edge;
	struct provenance_edge *other_caller_edge;
	struct provenance_edge *recursive_edge;
	char first_call;
	char second_call;
	bool existed;
	int error;

	callee = make_context(&callee_function);
	first = make_context(&first_function);
	second = make_context(&second_function);

	error = provenance_edge_create(callee, first,
	    (struct instruction *)&first_call, &first_edge, &existed);
	check(error == 0 && !existed, "create provenance edge");
	error = provenance_edge_create(callee, first,
	    (struct instruction *)&first_call, &same_edge, &existed);
	check(error == 0 && existed, "reuse provenance edge");
	check(same_edge == first_edge, "provenance edge is canonical");

	error = provenance_edge_create(callee, first,
	    (struct instruction *)&second_call, &other_site_edge, &existed);
	check(error == 0 && !existed, "distinguish call sites");
	check(other_site_edge != first_edge, "call sites have distinct edges");

	error = provenance_edge_create(callee, second,
	    (struct instruction *)&first_call, &other_caller_edge, &existed);
	check(error == 0 && !existed, "distinguish caller contexts");
	check(other_caller_edge != first_edge,
	    "caller contexts have distinct edges");

	error = provenance_edge_create(callee, callee,
	    (struct instruction *)&first_call, &recursive_edge, &existed);
	check(error == 0 && !existed, "record recursive provenance");
	check(provenance_edge_count(callee) == 4,
	    "callee records four provenance edges");
	{
		struct provenance_edge *edge;
		struct provenance_edge *previous = NULL;
		size_t count = 0;

		for (edge = provenance_edge_first(callee); edge != NULL;
		    edge = provenance_edge_next(callee, edge)) {
			check(previous == NULL ||
			    compare_edges(previous, edge) < 0,
			    "enumerate provenance edges in key order");
			previous = edge;
			count++;
		}
		check(count == 4, "enumerate every provenance edge");
	}

	error = context_create(&callee_function, NULL, callee->entry_state,
	    &same_context, &existed);
	check(error == 0 && existed, "reuse context after adding provenance");
	check(same_context == callee, "provenance does not change context identity");
	check(context_count(&callee_function) == 1,
	    "provenance does not add semantic contexts");

	context_collection_free(&second_function);
	context_collection_free(&first_function);
	context_collection_free(&callee_function);
}

int
main(void)
{
	test_provenance_edges();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
