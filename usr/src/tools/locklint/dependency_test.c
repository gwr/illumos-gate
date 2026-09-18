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
 * Exercise exit publication and continuation generation tracking without
 * performing semantic state transfer or Sparse CFG traversal.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "context.h"
#include "dependency.h"
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
test_dependency(void)
{
	struct function_info first_function = { 0 };
	struct function_info second_function = { 0 };
	struct semantic_state *first_state;
	struct semantic_state *second_state;
	struct semantic_state alternate_first_state = { 0 };
	struct function_context *first_context;
	struct function_context *second_context;
	struct context_exit *first_exit;
	struct context_exit *same_exit;
	struct context_exit *second_exit;
	struct continuation *direct;
	struct continuation *same_direct;
	struct continuation *first_to_second;
	struct continuation *second_to_first;
	char first_block;
	char second_block;
	struct analysis_point first_resume = {
		.block = (struct basic_block *)&first_block
	};
	struct analysis_point second_resume = {
		.block = (struct basic_block *)&second_block
	};
	bool created;
	int error;

	context_init(&first_function);
	context_init(&second_function);
	error = state_get_empty(&first_function, &first_state, &created);
	check(error == 0 && created, "create first semantic state");
	error = state_get_empty(&second_function, &second_state, &created);
	check(error == 0 && created, "create second semantic state");
	error = context_get(&first_function, NULL, first_state,
	    &first_context, &created);
	check(error == 0 && created, "create first context");
	error = context_get(&second_function, NULL, second_state,
	    &second_context, &created);
	check(error == 0 && created, "create second context");

	error = dependency_exit_publish(first_context, first_state, &first_exit,
	    &created);
	check(error == 0 && created, "publish first exit");
	check(first_exit->generation == 1, "first exit has generation one");
	error = dependency_exit_publish(first_context, first_state, &same_exit,
	    &created);
	check(error == 0 && !created, "reuse duplicate exit");
	check(same_exit == first_exit, "duplicate exit is canonical");
	check(first_context->exit_generation == 1,
	    "duplicate exit does not advance generation");

	error = dependency_continuation_get(first_context, first_context,
	    first_resume, first_state, NULL, &direct, &created);
	check(error == 0 && created, "register direct-recursive continuation");
	error = dependency_continuation_get(first_context, first_context,
	    first_resume, first_state, NULL, &same_direct, &created);
	check(error == 0 && !created, "reuse duplicate continuation");
	check(same_direct == direct, "duplicate continuation is canonical");
	check(dependency_continuation_next_exit(direct) == first_exit,
	    "late continuation sees existing exit");
	check(dependency_continuation_consume(direct, first_exit),
	    "consume first exit");
	check(dependency_continuation_next_exit(direct) == NULL,
	    "consumed continuation is current");
	check(!dependency_continuation_consume(direct, NULL),
	    "reject missing exit");

	error = dependency_continuation_get(second_context, first_context,
	    second_resume, first_state, NULL, &first_to_second, &created);
	check(error == 0 && created, "register first mutual continuation");
	error = dependency_continuation_get(first_context, second_context,
	    first_resume, second_state, NULL, &second_to_first, &created);
	check(error == 0 && created, "register second mutual continuation");

	error = dependency_exit_publish(second_context, second_state, &second_exit,
	    &created);
	check(error == 0 && created, "publish second context exit");
	check(dependency_continuation_next_exit(first_to_second) == second_exit,
	    "first mutual continuation sees callee exit");
	check(dependency_continuation_next_exit(second_to_first) == first_exit,
	    "second mutual continuation sees callee exit");
	check(!dependency_continuation_consume(first_to_second, first_exit),
	    "reject unrelated exit");
	check(dependency_continuation_consume(first_to_second, second_exit),
	    "consume mutual callee exit");

	/*
	 * Stand in for a second canonical state value, which will become
	 * constructible when semantic state gains nonempty fields.
	 */
	error = dependency_exit_publish(first_context, &alternate_first_state,
	    &second_exit, &created);
	check(error == 0 && created, "publish another distinct exit");
	check(second_exit->generation == 2, "second exit has generation two");
	check(dependency_continuation_next_exit(direct) == second_exit,
	    "existing continuation sees later exit");
	check(dependency_continuation_consume(direct, second_exit),
	    "consume later exit");
	check(dependency_exit_count(first_context) == 2,
	    "first context records two exits");
	check(dependency_continuation_count(first_context) == 2,
	    "first context records two continuations");

	context_fini(&second_function);
	context_fini(&first_function);
}

int
main(void)
{
	test_dependency();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
