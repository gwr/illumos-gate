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
 * Maintain the function-owned collections of caller contexts and interned
 * semantic states.  Keys are immutable after insertion, and provenance is
 * deliberately excluded from context comparison.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "avl.h"
#include "context.h"
#include "function_info.h"

static int
compare_semantic_state(const void *left_arg, const void *right_arg)
{
	(void) left_arg;
	(void) right_arg;

	/*
	 * The initial implementation has only one semantic value: empty.
	 */
	return (0);
}

static int
compare_function_context(const void *left_arg, const void *right_arg)
{
	const struct function_context *left = left_arg;
	const struct function_context *right = right_arg;
	int result;

	result = AVL_PCMP(left->bindings, right->bindings);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->entry_state, right->entry_state));
}

void
context_init(struct function_info *function)
{
	struct function_context_collection *collection = &function->contexts;

	avl_create(&collection->contexts, compare_function_context,
	    sizeof (struct function_context),
	    offsetof(struct function_context, by_key));
	avl_create(&collection->semantic_states, compare_semantic_state,
	    sizeof (struct semantic_state),
	    offsetof(struct semantic_state, by_value));
}

/*
 * Destroy the complete function-owned hierarchy.  Context keys refer to
 * interned states, so contexts must be released before semantic states.
 */
void
context_fini(struct function_info *function)
{
	struct function_context_collection *collection = &function->contexts;
	struct function_context *context;
	struct semantic_state *state;
	void *cookie = NULL;

	while ((context = avl_destroy_nodes(&collection->contexts,
	    &cookie)) != NULL)
		free(context);
	avl_destroy(&collection->contexts);

	cookie = NULL;
	while ((state = avl_destroy_nodes(&collection->semantic_states,
	    &cookie)) != NULL)
		free(state);
	avl_destroy(&collection->semantic_states);
}

/*
 * Return the function's canonical empty semantic state, creating it when
 * necessary.  Allocation failure leaves both output arguments unchanged.
 */
int
state_get_empty(struct function_info *function,
    struct semantic_state **result, bool *created)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_state key = { 0 };
	struct semantic_state *state;
	avl_index_t where;

	state = avl_find(&collection->semantic_states, &key, &where);
	if (state != NULL) {
		*result = state;
		*created = false;
		return (0);
	}
	state = calloc(1, sizeof (*state));
	if (state == NULL)
		return (ENOMEM);
	avl_insert(&collection->semantic_states, state, where);
	*result = state;
	*created = true;
	return (0);
}

/*
 * Find or create the context identified by canonical bindings and entry
 * state.  Allocation failure leaves both output arguments unchanged.
 */
int
context_get(struct function_info *function,
    const struct binding_environment *bindings,
    const struct semantic_state *entry_state,
    struct function_context **result, bool *created)
{
	struct function_context_collection *collection = &function->contexts;
	struct function_context key = {
		.function = function,
		.bindings = bindings,
		.entry_state = entry_state
	};
	struct function_context *context;
	avl_index_t where;

	context = avl_find(&collection->contexts, &key, &where);
	if (context != NULL) {
		*result = context;
		*created = false;
		return (0);
	}
	context = calloc(1, sizeof (*context));
	if (context == NULL)
		return (ENOMEM);
	context->function = function;
	context->bindings = bindings;
	context->entry_state = entry_state;
	avl_insert(&collection->contexts, context, where);
	*result = context;
	*created = true;
	return (0);
}

size_t
context_count(struct function_info *function)
{
	return (avl_numnodes(&function->contexts.contexts));
}

size_t
state_count(struct function_info *function)
{
	return (avl_numnodes(&function->contexts.semantic_states));
}
