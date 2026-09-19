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
 * Maintain the function-owned collections of caller contexts, semantic
 * states, and shared lock sets.  Keys are immutable after insertion, and
 * provenance is deliberately excluded from context comparison.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"
#include "context.h"
#include "dependency.h"
#include "function_info.h"
#include "provenance.h"

static int
compare_lock_identity(const struct lock_identity *left,
    const struct lock_identity *right)
{
	return (AVL_PCMP(left, right));
}

static int
compare_lock_set(const void *left_arg, const void *right_arg)
{
	const struct semantic_lock_set *left = left_arg;
	const struct semantic_lock_set *right = right_arg;
	size_t count = left->count < right->count ? left->count : right->count;
	size_t index;
	int result;

	for (index = 0; index < count; index++) {
		result = compare_lock_identity(left->entries[index].lock,
		    right->entries[index].lock);
		if (result != 0)
			return (result);
		if (left->entries[index].modes < right->entries[index].modes)
			return (-1);
		if (left->entries[index].modes > right->entries[index].modes)
			return (1);
	}
	if (left->count < right->count)
		return (-1);
	if (left->count > right->count)
		return (1);
	return (0);
}

static int
compare_semantic_state(const void *left_arg, const void *right_arg)
{
	const struct semantic_state *left = left_arg;
	const struct semantic_state *right = right_arg;

	return (AVL_PCMP(left->locks, right->locks));
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

static int
compare_point_state(const void *left_arg, const void *right_arg)
{
	const struct point_state *left = left_arg;
	const struct point_state *right = right_arg;
	int result;

	result = AVL_PCMP(left->point.block, right->point.block);
	if (result != 0)
		return (result);
	result = AVL_PCMP(left->point.next_instruction,
	    right->point.next_instruction);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->state, right->state));
}

void
context_collection_create(struct function_info *function)
{
	struct function_context_collection *collection = &function->contexts;

	avl_create(&collection->contexts, compare_function_context,
	    sizeof (struct function_context),
	    offsetof(struct function_context, by_key));
	avl_create(&collection->lock_sets, compare_lock_set,
	    sizeof (struct semantic_lock_set),
	    offsetof(struct semantic_lock_set, by_value));
	avl_create(&collection->semantic_states, compare_semantic_state,
	    sizeof (struct semantic_state),
	    offsetof(struct semantic_state, by_value));
}

static void
free_point_states(struct function_context *context)
{
	struct point_state *point_state;
	void *cookie = NULL;

	while ((point_state = avl_destroy_nodes(&context->point_states,
	    &cookie)) != NULL)
		free(point_state);
	avl_destroy(&context->point_states);
}

/*
 * Destroy the complete function-owned hierarchy.  Context keys refer to
 * interned states, so contexts must be released before semantic states.
 */
void
context_collection_free(struct function_info *function)
{
	struct function_context_collection *collection = &function->contexts;
	struct function_context *context;
	struct semantic_state *state;
	struct semantic_lock_set *locks;
	void *cookie = NULL;

	while ((context = avl_destroy_nodes(&collection->contexts,
	    &cookie)) != NULL) {
		free_point_states(context);
		dependency_records_free(context);
		provenance_edges_free(context);
		free(context);
	}
	avl_destroy(&collection->contexts);

	cookie = NULL;
	while ((state = avl_destroy_nodes(&collection->semantic_states,
	    &cookie)) != NULL)
		free(state);
	avl_destroy(&collection->semantic_states);

	cookie = NULL;
	while ((locks = avl_destroy_nodes(&collection->lock_sets,
	    &cookie)) != NULL)
		free(locks);
	avl_destroy(&collection->lock_sets);
}

/*
 * Return a canonical immutable copy of the supplied sorted lock array.
 */
static int
lock_set_intern(struct function_context_collection *collection,
    const struct semantic_lock_state *entries, size_t count,
    struct semantic_lock_set **result)
{
	struct semantic_lock_set *candidate;
	struct semantic_lock_set *locks;
	avl_index_t where;
	size_t size;

	if (count > LOCKLINT_MAX_TRACKED_LOCKS)
		return (E2BIG);
	size = sizeof (*candidate) +
	    count * sizeof (*entries);
	candidate = calloc(1, size);
	if (candidate == NULL)
		return (ENOMEM);
	candidate->count = count;
	if (count != 0)
		(void) memcpy(candidate->entries, entries,
		    count * sizeof (*entries));
	locks = avl_find(&collection->lock_sets, candidate, &where);
	if (locks != NULL) {
		free(candidate);
		*result = locks;
		return (0);
	}
	avl_insert(&collection->lock_sets, candidate, where);
	*result = candidate;
	return (0);
}

static int
semantic_state_intern(struct function_context_collection *collection,
    const struct semantic_lock_set *locks, struct semantic_state **result,
    bool *existed)
{
	struct semantic_state key = {
		.locks = locks
	};
	struct semantic_state *state;
	avl_index_t where;

	state = avl_find(&collection->semantic_states, &key, &where);
	if (state != NULL) {
		*result = state;
		*existed = true;
		return (0);
	}
	state = calloc(1, sizeof (*state));
	if (state == NULL)
		return (ENOMEM);
	state->locks = locks;
	avl_insert(&collection->semantic_states, state, where);
	*result = state;
	*existed = false;
	return (0);
}

/*
 * Return the function's canonical empty semantic state, creating it when
 * necessary.  Allocation failure leaves both output arguments unchanged.
 */
int
context_empty_state_intern(struct function_info *function,
    struct semantic_state **result, bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_lock_set *locks;
	int error;

	error = lock_set_intern(collection, NULL, 0, &locks);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, locks, result, existed));
}

/*
 * Return the destination function's canonical copy of an existing semantic
 * state.  The source state and its function-owned lock set remain unchanged.
 */
int
context_state_import(struct function_info *function,
    const struct semantic_state *source, struct semantic_state **result,
    bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_lock_set *locks;
	int error;

	if (source == NULL)
		return (EINVAL);
	error = lock_set_intern(collection, source->locks->entries,
	    source->locks->count, &locks);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, locks, result, existed));
}

/*
 * Return the canonical state produced by replacing one lock's modes.  Zero
 * modes removes the lock.  The current state remains unchanged.
 */
int
context_state_set_lock(struct function_info *function,
    const struct semantic_state *current, const struct lock_identity *lock,
    unsigned int modes, struct semantic_state **result, bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_lock_state entries[LOCKLINT_MAX_TRACKED_LOCKS];
	const struct semantic_lock_set *current_locks;
	struct semantic_lock_set *locks;
	size_t current_index = 0;
	size_t result_count;
	size_t result_index = 0;
	bool found = false;
	int error;

	if (current == NULL || lock == NULL)
		return (EINVAL);
	current_locks = current->locks;
	while (current_index < current_locks->count) {
		int order = compare_lock_identity(
		    current_locks->entries[current_index].lock, lock);

		if (order >= 0) {
			found = order == 0;
			break;
		}
		current_index++;
	}
	if (found && current_locks->entries[current_index].modes == modes) {
		return (semantic_state_intern(collection, current_locks, result,
		    existed));
	}
	if (!found && modes == 0) {
		return (semantic_state_intern(collection, current_locks, result,
		    existed));
	}
	if (!found && current_locks->count == LOCKLINT_MAX_TRACKED_LOCKS)
		return (E2BIG);

	result_count = current_locks->count +
	    (!found && modes != 0 ? 1 : 0) - (found && modes == 0 ? 1 : 0);
	while (result_index < current_index) {
		entries[result_index] = current_locks->entries[result_index];
		result_index++;
	}
	if (modes != 0) {
		entries[result_index].lock = lock;
		entries[result_index].modes = modes;
		result_index++;
	}
	if (found)
		current_index++;
	while (current_index < current_locks->count)
		entries[result_index++] = current_locks->entries[current_index++];

	error = lock_set_intern(collection, entries, result_count, &locks);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, locks, result, existed));
}

/*
 * Find or create the context identified by canonical bindings and entry
 * state.  Allocation failure leaves both output arguments unchanged.
 */
int
context_create(struct function_info *function,
    const struct binding_environment *bindings,
    const struct semantic_state *entry_state,
    struct function_context **result, bool *existed)
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
		*existed = true;
		return (0);
	}
	context = calloc(1, sizeof (*context));
	if (context == NULL)
		return (ENOMEM);
	context->function = function;
	context->bindings = bindings;
	context->entry_state = entry_state;
	avl_create(&context->point_states, compare_point_state,
	    sizeof (struct point_state), offsetof(struct point_state, by_key));
	dependency_records_create(context);
	provenance_edges_create(context);
	avl_insert(&collection->contexts, context, where);
	*result = context;
	*existed = false;
	return (0);
}

/*
 * Record one reached state at an analysis point, or return the existing
 * canonical record.  Allocation failure leaves both output arguments
 * unchanged.
 */
int
context_point_state_record(struct function_context *context,
    struct analysis_point point, const struct semantic_state *state,
    struct point_state **result, bool *existed)
{
	struct point_state key = {
		.context = context,
		.point = point,
		.state = state
	};
	struct point_state *point_state;
	avl_index_t where;

	point_state = avl_find(&context->point_states, &key, &where);
	if (point_state != NULL) {
		*result = point_state;
		*existed = true;
		return (0);
	}
	point_state = calloc(1, sizeof (*point_state));
	if (point_state == NULL)
		return (ENOMEM);
	point_state->context = context;
	point_state->point = point;
	point_state->state = state;
	avl_insert(&context->point_states, point_state, where);
	*result = point_state;
	*existed = false;
	return (0);
}

size_t
context_count(struct function_info *function)
{
	return (avl_numnodes(&function->contexts.contexts));
}

size_t
context_state_count(struct function_info *function)
{
	return (avl_numnodes(&function->contexts.semantic_states));
}

size_t
context_lock_set_count(struct function_info *function)
{
	return (avl_numnodes(&function->contexts.lock_sets));
}

size_t
context_state_lock_count(const struct semantic_state *state)
{
	return (state->locks->count);
}

unsigned int
context_state_lock_modes(const struct semantic_state *state,
    const struct lock_identity *lock)
{
	size_t index;

	for (index = 0; index < state->locks->count; index++) {
		int order = compare_lock_identity(state->locks->entries[index].lock,
		    lock);

		if (order == 0)
			return (state->locks->entries[index].modes);
		if (order > 0)
			break;
	}
	return (0);
}

size_t
context_point_state_count(struct function_context *context)
{
	return (avl_numnodes(&context->point_states));
}
