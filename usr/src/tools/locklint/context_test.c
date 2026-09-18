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
#include "function_info.h"

static unsigned int failures;

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
	bool created;
	int error;

	context_init(&function);

	error = state_get_empty(&function, &state, &created);
	check(error == 0, "create empty semantic state");
	check(created, "empty semantic state reported as new");
	check(state_count(&function) == 1,
	    "function has one semantic state");

	error = state_get_empty(&function, &same, &created);
	check(error == 0, "find empty semantic state");
	check(!created, "existing empty semantic state reported as reused");
	check(same == state, "empty semantic state is interned");

	context_fini(&function);
}

static void
test_context_interning(void)
{
	struct function_info function = { 0 };
	struct semantic_state *state;
	struct function_context *context;
	struct function_context *same;
	bool created;
	int error;

	context_init(&function);
	error = state_get_empty(&function, &state, &created);
	check(error == 0, "create context entry state");

	error = context_get(&function, NULL, state, &context, &created);
	check(error == 0, "create function context");
	check(created, "function context reported as new");
	check(context->function == &function,
	    "context records its owning function");
	check(context_count(&function) == 1, "function has one context");

	error = context_get(&function, NULL, state, &same, &created);
	check(error == 0, "find function context");
	check(!created, "existing function context reported as reused");
	check(same == context, "function context is canonical");

	context_fini(&function);
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
	bool created;
	int error;

	context_init(&first);
	context_init(&second);
	error = state_get_empty(&first, &first_state, &created);
	check(error == 0, "create first function state");

	error = state_get_empty(&second, &second_state, &created);
	check(error == 0, "create second function state");
	check(created, "second function owns a distinct semantic state");
	check(second_state != first_state,
	    "semantic states are interned per function");

	error = context_get(&first, NULL, first_state, &first_context, &created);
	check(error == 0, "create first function context");
	error = context_get(&second, NULL, second_state,
	    &second_context, &created);
	check(error == 0, "create second function context");
	check(created, "second function owns a distinct context");
	check(second_context != first_context,
	    "contexts are collected per function");
	check(context_count(&first) == 1,
	    "second function does not change first context count");
	check(context_count(&second) == 1,
	    "second function has one context");

	context_fini(&second);
	context_fini(&first);
}

int
main(void)
{
	test_state_interning();
	test_context_interning();
	test_function_ownership();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
