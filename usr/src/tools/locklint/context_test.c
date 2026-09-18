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
 * Exercise function-owned context lookup and semantic-state interning
 * independently of Sparse parsing and checker behavior.
 */

#include <stdbool.h>
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

void
provenance_fini(struct function_context *context)
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

static void
test_state_interning(void)
{
	struct function_info function = { 0 };
	struct semantic_state *state;
	struct semantic_state *same;
	bool existed;
	int error;

	context_collection_create(&function);

	error = context_empty_state_intern(&function, &state, &existed);
	check(error == 0, "create empty semantic state");
	check(!existed, "empty semantic state reported as new");
	check(context_state_count(&function) == 1,
	    "function has one semantic state");

	error = context_empty_state_intern(&function, &same, &existed);
	check(error == 0, "find empty semantic state");
	check(existed, "existing empty semantic state reported as reused");
	check(same == state, "empty semantic state is interned");

	context_collection_free(&function);
}

static void
test_context_interning(void)
{
	struct function_info function = { 0 };
	struct semantic_state *state;
	struct function_context *context;
	struct function_context *same;
	bool existed;
	int error;

	context_collection_create(&function);
	error = context_empty_state_intern(&function, &state, &existed);
	check(error == 0, "create context entry state");

	error = context_create(&function, NULL, state, &context, &existed);
	check(error == 0, "create function context");
	check(!existed, "function context reported as new");
	check(context->function == &function,
	    "context records its owning function");
	check(context_count(&function) == 1, "function has one context");

	error = context_create(&function, NULL, state, &same, &existed);
	check(error == 0, "find function context");
	check(existed, "existing function context reported as reused");
	check(same == context, "function context is canonical");

	context_collection_free(&function);
}

static void
test_function_ownership(void)
{
	struct function_info first = { 0 };
	struct function_info second = { 0 };
	struct semantic_state *first_state;
	struct semantic_state *second_state;
	struct function_context *first_context;
	struct function_context *second_context;
	bool existed;
	int error;

	context_collection_create(&first);
	context_collection_create(&second);
	error = context_empty_state_intern(&first, &first_state, &existed);
	check(error == 0, "create first function state");

	error = context_empty_state_intern(&second, &second_state, &existed);
	check(error == 0, "create second function state");
	check(!existed, "second function owns a distinct semantic state");
	check(second_state != first_state,
	    "semantic states are interned per function");

	error = context_create(&first, NULL, first_state, &first_context,
	    &existed);
	check(error == 0, "create first function context");
	error = context_create(&second, NULL, second_state, &second_context,
	    &existed);
	check(error == 0, "create second function context");
	check(!existed, "second function owns a distinct context");
	check(second_context != first_context,
	    "contexts are collected per function");
	check(context_count(&first) == 1,
	    "second function does not change first context count");
	check(context_count(&second) == 1,
	    "second function has one context");

	context_collection_free(&second);
	context_collection_free(&first);
}

static void
test_point_state_interning(void)
{
	struct function_info function = { 0 };
	struct semantic_state *state;
	struct function_context *context;
	struct point_state *point_state;
	struct point_state *same;
	struct point_state *other;
	char block;
	char instruction;
	struct analysis_point point = {
		.block = (struct basic_block *)&block,
		.next_instruction = (struct instruction *)&instruction
	};
	struct analysis_point block_exit = {
		.block = (struct basic_block *)&block
	};
	bool existed;
	int error;

	context_collection_create(&function);
	error = context_empty_state_intern(&function, &state, &existed);
	check(error == 0, "create point state value");
	error = context_create(&function, NULL, state, &context, &existed);
	check(error == 0, "create point state context");

	error = context_point_state_record(context, point, state, &point_state,
	    &existed);
	check(error == 0 && !existed, "record new point state");
	error = context_point_state_record(context, point, state, &same, &existed);
	check(error == 0 && existed, "reuse recorded point state");
	check(same == point_state, "point state is canonical");

	error = context_point_state_record(context, block_exit, state, &other,
	    &existed);
	check(error == 0 && !existed, "record state at another point");
	check(other != point_state, "analysis points remain distinct");
	check(context_point_state_count(context) == 2,
	    "context has two point states");

	context_collection_free(&function);
}

int
main(void)
{
	test_state_interning();
	test_context_interning();
	test_function_ownership();
	test_point_state_interning();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
