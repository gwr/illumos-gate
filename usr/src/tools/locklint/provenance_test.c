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
#include "statistics.h"

static unsigned int failures;

void
dependency_records_create(struct function_context *context)
{
	(void) context;
}

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
make_context_kind(struct function_info *function, bool root)
{
	struct semantic_state *state = NULL;
	struct function_context *context = NULL;
	bool existed;
	int error;

	context_collection_create(function);
	error = context_empty_state_intern(function, &state, &existed);
	check(error == 0 && !existed, "create semantic state");
	if (root) {
		error = context_root_create(function, NULL, state, &context,
		    &existed);
	} else {
		error = context_create(function, NULL, state, &context, &existed);
	}
	check(error == 0 && !existed, "create function context");
	return (context);
}

static struct function_context *
make_context(struct function_info *function)
{
	return (make_context_kind(function, false));
}

static void
add_edge(struct function_context *callee, struct function_context *caller,
    void *instruction)
{
	struct provenance_edge *edge;
	bool existed;
	int error;

	error = provenance_edge_create(callee, caller, instruction, &edge,
	    &existed);
	check(error == 0 && !existed, "add traversal provenance edge");
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

struct root_call_results {
	struct function_context *first_root;
	struct instruction *first_instruction;
	struct function_context *second_root;
	struct instruction *second_instruction;
	size_t first_count;
	size_t second_count;
	size_t unexpected_count;
};

static void
record_root_call(struct function_context *context,
    struct instruction *instruction, void *data_arg)
{
	struct root_call_results *results = data_arg;

	if (context == results->first_root &&
	    instruction == results->first_instruction) {
		results->first_count++;
	} else if (context == results->second_root &&
	    instruction == results->second_instruction) {
		results->second_count++;
	} else {
		results->unexpected_count++;
	}
}

static void
test_root_call_traversal(void)
{
	struct function_info target_function = { 0 };
	struct function_info left_function = { 0 };
	struct function_info right_function = { 0 };
	struct function_info first_root_function = { 0 };
	struct function_info second_root_function = { 0 };
	struct function_context *target;
	struct function_context *left;
	struct function_context *right;
	struct function_context *first_root;
	struct function_context *second_root;
	struct root_call_results results;
	char target_from_left;
	char target_from_right;
	char recursive_left;
	char recursive_right;
	char first_root_call;
	char second_root_call;
	size_t requests = statistics.caller_recovery_requests;
	size_t unique_starts = statistics.caller_recovery_unique_starts;
	size_t contexts_visited = statistics.caller_recovery_contexts_visited;
	size_t unique_contexts =
	    statistics.caller_recovery_unique_contexts_visited;
	size_t edges_examined = statistics.caller_recovery_edges_examined;
	size_t root_calls = statistics.caller_recovery_root_calls;
	bool found;
	int error;

	target = make_context(&target_function);
	left = make_context(&left_function);
	right = make_context(&right_function);
	first_root = make_context_kind(&first_root_function, true);
	second_root = make_context_kind(&second_root_function, true);

	add_edge(target, left, &target_from_left);
	add_edge(target, right, &target_from_right);
	add_edge(left, right, &recursive_left);
	add_edge(right, left, &recursive_right);
	add_edge(left, first_root, &first_root_call);
	add_edge(right, first_root, &first_root_call);
	add_edge(right, second_root, &second_root_call);

	results = (struct root_call_results) {
		.first_root = first_root,
		.first_instruction = (struct instruction *)&first_root_call,
		.second_root = second_root,
		.second_instruction = (struct instruction *)&second_root_call
	};
	error = provenance_for_each_root_call(target, record_root_call, &results,
	    &found);
	check(error == 0, "traverse converging recursive provenance");
	check(found, "find root calls through provenance");
	check(results.first_count == 1,
	    "deduplicate root call reached by converging paths");
	check(results.second_count == 1, "visit distinct root call");
	check(results.unexpected_count == 0, "visit only expected root calls");

	error = provenance_for_each_root_call(target, record_root_call, &results,
	    &found);
	check(error == 0 && found, "repeat root call traversal");
	check(results.first_count == 2 && results.second_count == 2,
	    "repeat cached-candidate traversal result");

	found = true;
	error = provenance_for_each_root_call(first_root, record_root_call,
	    &results, &found);
	check(error == 0, "traverse context without incoming provenance");
	check(!found, "report no root call for isolated root context");
	check(context_count(&target_function) == 1 &&
	    context_count(&left_function) == 1 &&
	    context_count(&right_function) == 1,
	    "provenance traversal does not add semantic contexts");
	check(statistics.caller_recovery_requests - requests == 3,
	    "count caller recovery requests");
	check(statistics.caller_recovery_unique_starts - unique_starts == 2,
	    "count unique caller recovery starts");
	check(statistics.caller_recovery_contexts_visited -
	    contexts_visited == 7, "count caller recovery context visits");
	check(statistics.caller_recovery_unique_contexts_visited -
	    unique_contexts == 4, "count unique caller recovery contexts");
	check(statistics.caller_recovery_edges_examined -
	    edges_examined == 14, "count examined caller recovery edges");
	check(statistics.caller_recovery_root_calls - root_calls == 4,
	    "count caller recovery result calls");
	check(statistics.caller_recovery_first_max_depth.samples == 2 &&
	    statistics.caller_recovery_first_max_depth.total == 2 &&
	    statistics.caller_recovery_first_max_depth.maximum == 2,
	    "measure first-query maximum depth");
	check(statistics.caller_recovery_repeat_max_depth.samples == 1 &&
	    statistics.caller_recovery_repeat_max_depth.total == 2 &&
	    statistics.caller_recovery_repeat_max_depth.maximum == 2,
	    "measure repeated-query maximum depth");
	check(statistics.caller_recovery_first_contexts_visited.samples == 2 &&
	    statistics.caller_recovery_first_contexts_visited.total == 4 &&
	    statistics.caller_recovery_first_contexts_visited.maximum == 3,
	    "measure first-query context visits");
	check(statistics.caller_recovery_repeat_contexts_visited.samples == 1 &&
	    statistics.caller_recovery_repeat_contexts_visited.total == 3 &&
	    statistics.caller_recovery_repeat_contexts_visited.maximum == 3,
	    "measure repeated-query context visits");
	check(statistics.caller_recovery_first_edges_examined.samples == 2 &&
	    statistics.caller_recovery_first_edges_examined.total == 7 &&
	    statistics.caller_recovery_first_edges_examined.maximum == 7,
	    "measure first-query examined edges");
	check(statistics.caller_recovery_repeat_edges_examined.samples == 1 &&
	    statistics.caller_recovery_repeat_edges_examined.total == 7 &&
	    statistics.caller_recovery_repeat_edges_examined.maximum == 7,
	    "measure repeated-query examined edges");
	check(statistics.caller_recovery_first_root_calls.samples == 2 &&
	    statistics.caller_recovery_first_root_calls.total == 2 &&
	    statistics.caller_recovery_first_root_calls.maximum == 2,
	    "measure first-query root calls");
	check(statistics.caller_recovery_repeat_root_calls.samples == 1 &&
	    statistics.caller_recovery_repeat_root_calls.total == 2 &&
	    statistics.caller_recovery_repeat_root_calls.maximum == 2,
	    "measure repeated-query root calls");

	context_collection_free(&second_root_function);
	context_collection_free(&first_root_function);
	context_collection_free(&right_function);
	context_collection_free(&left_function);
	context_collection_free(&target_function);
}

int
main(void)
{
	test_provenance_edges();
	test_root_call_traversal();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
