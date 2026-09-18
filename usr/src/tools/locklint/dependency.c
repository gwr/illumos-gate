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
 * Maintain function-context exits and caller continuations.  These
 * owner-local lists record dependency progress without performing semantic
 * exit-to-caller state mapping or global worklist scheduling.
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "context.h"
#include "dependency.h"
#include "worklist.h"

void
dependency_records_free(struct function_context *context)
{
	while (context->continuations != NULL) {
		struct continuation *next = context->continuations->next;

		free(context->continuations);
		context->continuations = next;
	}
	while (context->exits != NULL) {
		struct context_exit *next = context->exits->next;

		free(context->exits);
		context->exits = next;
	}
	context->exit_generation = 0;
}

/*
 * Publish a canonical exit state once.  Allocation or generation failure
 * leaves both output arguments and the context unchanged.
 */
int
dependency_exit_publish(struct function_context *context,
    const struct semantic_state *state, struct context_exit **result,
    bool *existed)
{
	struct context_exit *exit;

	for (exit = context->exits; exit != NULL; exit = exit->next) {
		if (exit->state == state) {
			*result = exit;
			*existed = true;
			return (0);
		}
	}
	if (context->exit_generation == UINT_MAX)
		return (EOVERFLOW);
	exit = calloc(1, sizeof (*exit));
	if (exit == NULL)
		return (ENOMEM);
	exit->state = state;
	exit->generation = context->exit_generation + 1;
	exit->next = context->exits;
	context->exits = exit;
	context->exit_generation = exit->generation;
	*result = exit;
	*existed = false;
	return (0);
}

static bool
same_continuation(const struct continuation *continuation,
    struct function_context *caller_context, struct analysis_point resume_point,
    const struct semantic_state *caller_state,
    const struct binding_environment *callee_bindings)
{
	return (continuation->caller_context == caller_context &&
	    continuation->resume_point.block == resume_point.block &&
	    continuation->resume_point.next_instruction ==
	    resume_point.next_instruction &&
	    continuation->caller_state == caller_state &&
	    continuation->callee_bindings == callee_bindings);
}

/*
 * Register one caller dependency on a callee context.  A new continuation
 * starts at generation zero so it can consume exits published before the
 * dependency was discovered.
 */
int
dependency_continuation_create(struct function_context *callee_context,
    struct function_context *caller_context, struct analysis_point resume_point,
    const struct semantic_state *caller_state,
    const struct binding_environment *callee_bindings,
    struct continuation **result, bool *existed)
{
	struct continuation *continuation;

	for (continuation = callee_context->continuations;
	    continuation != NULL; continuation = continuation->next) {
		if (same_continuation(continuation, caller_context, resume_point,
		    caller_state, callee_bindings)) {
			*result = continuation;
			*existed = true;
			return (0);
		}
	}
	continuation = calloc(1, sizeof (*continuation));
	if (continuation == NULL)
		return (ENOMEM);
	continuation->callee_context = callee_context;
	continuation->caller_context = caller_context;
	continuation->resume_point = resume_point;
	continuation->caller_state = caller_state;
	continuation->callee_bindings = callee_bindings;
	continuation->next = callee_context->continuations;
	callee_context->continuations = continuation;
	*result = continuation;
	*existed = false;
	return (0);
}

/*
 * Return the oldest exit not yet consumed by this continuation.
 */
const struct context_exit *
dependency_continuation_next_exit(const struct continuation *continuation)
{
	const struct context_exit *exit;
	const struct context_exit *next = NULL;

	for (exit = continuation->callee_context->exits;
	    exit != NULL; exit = exit->next) {
		if (exit->generation <= continuation->last_consumed_generation)
			continue;
		if (next == NULL || exit->generation < next->generation)
			next = exit;
	}
	return (next);
}

/*
 * Apply the next unconsumed callee exit to its continuation.  The caller
 * state must already be mapped and canonical.  Do not advance the
 * continuation unless the destination point state has been retained.
 */
int
dependency_continuation_apply_exit(struct continuation *continuation,
    const struct context_exit *exit,
    const struct semantic_state *mapped_caller_state, struct worklist *worklist,
    struct point_state **result, bool *existed)
{
	struct point_state *point_state;
	bool point_existed;
	int error;

	if (exit == NULL ||
	    dependency_continuation_next_exit(continuation) != exit)
		return (EINVAL);
	error = context_point_state_record(continuation->caller_context,
	    continuation->resume_point, mapped_caller_state, &point_state,
	    &point_existed);
	if (error != 0)
		return (error);
	if (!point_existed)
		(void) worklist_point_state_enqueue(worklist, point_state);
	continuation->last_consumed_generation = exit->generation;
	*result = point_state;
	*existed = point_existed;
	return (0);
}

size_t
dependency_exit_count(const struct function_context *context)
{
	const struct context_exit *exit;
	size_t count = 0;

	for (exit = context->exits; exit != NULL; exit = exit->next)
		count++;
	return (count);
}

size_t
dependency_continuation_count(const struct function_context *context)
{
	const struct continuation *continuation;
	size_t count = 0;

	for (continuation = context->continuations;
	    continuation != NULL; continuation = continuation->next)
		count++;
	return (count);
}
