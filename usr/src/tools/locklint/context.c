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
 * states, and shared lock and visibility sets.  Keys are immutable after
 * insertion, and provenance is deliberately excluded from context comparison.
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
#include "statistics.h"

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
compare_visibility_region(const struct visibility_region *left,
    const struct visibility_region *right)
{
	int result;

	result = AVL_PCMP(left->analysis_object, right->analysis_object);
	if (result != 0)
		return (result);
	if (left->target_offset < right->target_offset)
		return (-1);
	if (left->target_offset > right->target_offset)
		return (1);
	if (left->target_length < right->target_length)
		return (-1);
	if (left->target_length > right->target_length)
		return (1);
	return (0);
}

static bool
visibility_region_valid(const struct visibility_region *region)
{
	return (region != NULL && region->analysis_object != NULL &&
	    region->target_length != 0 &&
	    region->target_length <= INT64_MAX &&
	    region->target_offset <=
	    INT64_MAX - (int64_t)(region->target_length - 1));
}

static bool
visibility_region_subsumes(const struct visibility_region *outer,
    const struct visibility_region *inner)
{
	uint64_t displacement;

	if (!visibility_region_valid(outer) || !visibility_region_valid(inner) ||
	    outer->analysis_object != inner->analysis_object ||
	    outer->target_offset > inner->target_offset)
		return (false);
	displacement = (uint64_t)inner->target_offset -
	    (uint64_t)outer->target_offset;
	return (displacement < outer->target_length &&
	    inner->target_length <= outer->target_length - displacement);
}

static int
compare_visibility_set(const void *left_arg, const void *right_arg)
{
	const struct semantic_visibility_set *left = left_arg;
	const struct semantic_visibility_set *right = right_arg;
	size_t count = left->count < right->count ? left->count : right->count;
	size_t index;
	int result;

	for (index = 0; index < count; index++) {
		result = compare_visibility_region(&left->entries[index].region,
		    &right->entries[index].region);
		if (result != 0)
			return (result);
		if (left->entries[index].visibility <
		    right->entries[index].visibility)
			return (-1);
		if (left->entries[index].visibility >
		    right->entries[index].visibility)
			return (1);
	}
	if (left->count < right->count)
		return (-1);
	if (left->count > right->count)
		return (1);
	return (0);
}

static int
compare_target_set(const void *left_arg, const void *right_arg)
{
	const struct semantic_target_set *left = left_arg;
	const struct semantic_target_set *right = right_arg;
	size_t count = left->count < right->count ? left->count : right->count;
	size_t index;
	int result;

	for (index = 0; index < count; index++) {
		result = compare_visibility_region(&left->entries[index].region,
		    &right->entries[index].region);
		if (result != 0)
			return (result);
		result = AVL_PCMP(left->entries[index].targets,
		    right->entries[index].targets);
		if (result != 0)
			return (result);
	}
	if (left->count < right->count)
		return (-1);
	if (left->count > right->count)
		return (1);
	return (0);
}

static int
compare_alias_set(const void *left_arg, const void *right_arg)
{
	const struct semantic_alias_set *left = left_arg;
	const struct semantic_alias_set *right = right_arg;
	size_t count = left->count < right->count ? left->count : right->count;
	size_t index;
	int result;

	for (index = 0; index < count; index++) {
		result = AVL_PCMP(left->entries[index].source,
		    right->entries[index].source);
		if (result != 0)
			return (result);
		result = AVL_PCMP(left->entries[index].target,
		    right->entries[index].target);
		if (result != 0)
			return (result);
	}
	if (left->count < right->count)
		return (-1);
	if (left->count > right->count)
		return (1);
	return (0);
}

static int
compare_competition(const struct competition_interval *left,
    const struct competition_interval *right)
{
	if (left->entry_condition != right->entry_condition)
		return (left->entry_condition ? 1 : -1);
	if (left->minimum_unbounded != right->minimum_unbounded)
		return (left->minimum_unbounded ? -1 : 1);
	if (!left->minimum_unbounded && left->minimum < right->minimum)
		return (-1);
	if (!left->minimum_unbounded && left->minimum > right->minimum)
		return (1);
	if (left->maximum_unbounded != right->maximum_unbounded)
		return (left->maximum_unbounded ? 1 : -1);
	if (!left->maximum_unbounded && left->maximum < right->maximum)
		return (-1);
	if (!left->maximum_unbounded && left->maximum > right->maximum)
		return (1);
	return (0);
}

static int
compare_semantic_state(const void *left_arg, const void *right_arg)
{
	const struct semantic_state *left = left_arg;
	const struct semantic_state *right = right_arg;
	int order;

	order = compare_competition(&left->competition, &right->competition);
	if (order != 0)
		return (order);
	order = AVL_PCMP(left->locks, right->locks);
	if (order != 0)
		return (order);
	order = AVL_PCMP(left->visibility, right->visibility);
	if (order != 0)
		return (order);
	order = AVL_PCMP(left->targets, right->targets);
	if (order != 0)
		return (order);
	order = AVL_PCMP(left->aliases, right->aliases);
	if (order != 0)
		return (order);
	return (AVL_PCMP(left->operation_profile, right->operation_profile));
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
	result = AVL_PCMP(left->entry_state, right->entry_state);
	if (result != 0)
		return (result);
	if (left->kind < right->kind)
		return (-1);
	if (left->kind > right->kind)
		return (1);
	return (0);
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
	result = AVL_PCMP(left->point.conditional_instruction,
	    right->point.conditional_instruction);
	if (result != 0)
		return (result);
	if (left->point.conditional_nonzero !=
	    right->point.conditional_nonzero) {
		return (left->point.conditional_nonzero ? 1 : -1);
	}
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
	avl_create(&collection->visibility_sets, compare_visibility_set,
	    sizeof (struct semantic_visibility_set),
	    offsetof(struct semantic_visibility_set, by_value));
	avl_create(&collection->target_sets, compare_target_set,
	    sizeof (struct semantic_target_set),
	    offsetof(struct semantic_target_set, by_value));
	avl_create(&collection->alias_sets, compare_alias_set,
	    sizeof (struct semantic_alias_set),
	    offsetof(struct semantic_alias_set, by_value));
	avl_create(&collection->semantic_states, compare_semantic_state,
	    sizeof (struct semantic_state),
	    offsetof(struct semantic_state, by_value));
}

static void
free_point_states(struct function_context *context)
{
	struct point_state *point_state;
	void *cookie = NULL;

	statistics.cleanup_point_states_enum++;
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
	struct semantic_visibility_set *visibility;
	struct semantic_target_set *targets;
	struct semantic_alias_set *aliases;
	void *cookie = NULL;

	statistics.cleanup_contexts_enum++;
	while ((context = avl_destroy_nodes(&collection->contexts,
	    &cookie)) != NULL) {
		free_point_states(context);
		dependency_records_free(context);
		provenance_edges_free(context);
		free(context);
	}
	avl_destroy(&collection->contexts);

	cookie = NULL;
	statistics.cleanup_semantic_states_enum++;
	while ((state = avl_destroy_nodes(&collection->semantic_states,
	    &cookie)) != NULL)
		free(state);
	avl_destroy(&collection->semantic_states);

	cookie = NULL;
	while ((locks = avl_destroy_nodes(&collection->lock_sets,
	    &cookie)) != NULL)
		free(locks);
	avl_destroy(&collection->lock_sets);

	cookie = NULL;
	while ((visibility = avl_destroy_nodes(&collection->visibility_sets,
	    &cookie)) != NULL)
		free(visibility);
	avl_destroy(&collection->visibility_sets);

	cookie = NULL;
	while ((targets = avl_destroy_nodes(&collection->target_sets,
	    &cookie)) != NULL)
		free(targets);
	avl_destroy(&collection->target_sets);

	cookie = NULL;
	while ((aliases = avl_destroy_nodes(&collection->alias_sets,
	    &cookie)) != NULL)
		free(aliases);
	avl_destroy(&collection->alias_sets);
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

/*
 * Return a canonical immutable copy of the supplied sorted visibility array.
 */
static int
visibility_set_intern(struct function_context_collection *collection,
    const struct semantic_visibility_state *entries, size_t count,
    struct semantic_visibility_set **result)
{
	struct semantic_visibility_set *candidate;
	struct semantic_visibility_set *visibility;
	avl_index_t where;
	size_t size;

	if (count > LOCKLINT_MAX_TRACKED_VISIBILITY)
		return (E2BIG);
	size = sizeof (*candidate) + count * sizeof (*entries);
	candidate = calloc(1, size);
	if (candidate == NULL)
		return (ENOMEM);
	candidate->count = count;
	if (count != 0)
		(void) memcpy(candidate->entries, entries,
		    count * sizeof (*entries));
	visibility = avl_find(&collection->visibility_sets, candidate, &where);
	if (visibility != NULL) {
		free(candidate);
		collection->visibility_sets_reused++;
		*result = visibility;
		return (0);
	}
	avl_insert(&collection->visibility_sets, candidate, where);
	collection->visibility_sets_created++;
	*result = candidate;
	return (0);
}

static int
target_set_intern(struct function_context_collection *collection,
    const struct semantic_target_state *entries, size_t count,
    struct semantic_target_set **result)
{
	struct semantic_target_set *candidate;
	struct semantic_target_set *targets;
	avl_index_t where;
	size_t size;

	if (count > LOCKLINT_MAX_TRACKED_TARGETS)
		return (E2BIG);
	size = sizeof (*candidate) + count * sizeof (*entries);
	candidate = calloc(1, size);
	if (candidate == NULL)
		return (ENOMEM);
	candidate->count = count;
	if (count != 0)
		(void) memcpy(candidate->entries, entries,
		    count * sizeof (*entries));
	targets = avl_find(&collection->target_sets, candidate, &where);
	if (targets != NULL) {
		free(candidate);
		*result = targets;
		return (0);
	}
	avl_insert(&collection->target_sets, candidate, where);
	*result = candidate;
	return (0);
}

static int
alias_set_intern(struct function_context_collection *collection,
    const struct semantic_alias_state *entries, size_t count,
    struct semantic_alias_set **result)
{
	struct semantic_alias_set *candidate;
	struct semantic_alias_set *aliases;
	avl_index_t where;
	size_t size;

	if (count > LOCKLINT_MAX_TRACKED_ALIASES)
		return (E2BIG);
	size = sizeof (*candidate) + count * sizeof (*entries);
	candidate = calloc(1, size);
	if (candidate == NULL)
		return (ENOMEM);
	candidate->count = count;
	if (count != 0)
		(void) memcpy(candidate->entries, entries,
		    count * sizeof (*entries));
	aliases = avl_find(&collection->alias_sets, candidate, &where);
	if (aliases != NULL) {
		free(candidate);
		*result = aliases;
		return (0);
	}
	avl_insert(&collection->alias_sets, candidate, where);
	*result = candidate;
	return (0);
}

static int
semantic_state_intern(struct function_context_collection *collection,
    const struct semantic_lock_set *locks,
    const struct semantic_visibility_set *visibility,
    const struct semantic_target_set *targets,
    const struct semantic_alias_set *aliases,
    const struct operation_family_profile *operation_profile,
    struct competition_interval competition,
    struct semantic_state **result, bool *existed)
{
	struct semantic_state key = {
		.locks = locks,
		.visibility = visibility,
		.targets = targets,
		.aliases = aliases,
		.operation_profile = operation_profile,
		.competition = competition
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
	state->visibility = visibility;
	state->targets = targets;
	state->aliases = aliases;
	state->operation_profile = operation_profile;
	state->competition = competition;
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
	struct semantic_visibility_set *visibility;
	struct semantic_target_set *targets;
	struct semantic_alias_set *aliases;
	int error;

	error = lock_set_intern(collection, NULL, 0, &locks);
	if (error != 0)
		return (error);
	error = visibility_set_intern(collection, NULL, 0, &visibility);
	if (error != 0)
		return (error);
	error = target_set_intern(collection, NULL, 0, &targets);
	if (error != 0)
		return (error);
	error = alias_set_intern(collection, NULL, 0, &aliases);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, locks, visibility, targets,
	    aliases, NULL, (struct competition_interval){ 0 }, result,
	    existed));
}

/*
 * Return the canonical root-entry state.  Its distinguished condition is
 * consumed by the first competition transition rather than shifted as an
 * ordinary merged interval.
 */
int
context_entry_state_intern(struct function_info *function,
    struct semantic_state **result, bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct competition_interval competition = {
		.minimum = 0,
		.maximum = 1,
		.entry_condition = true
	};
	struct semantic_lock_set *locks;
	struct semantic_visibility_set *visibility;
	struct semantic_target_set *targets;
	struct semantic_alias_set *aliases;
	int error;

	error = lock_set_intern(collection, NULL, 0, &locks);
	if (error != 0)
		return (error);
	error = visibility_set_intern(collection, NULL, 0, &visibility);
	if (error != 0)
		return (error);
	error = target_set_intern(collection, NULL, 0, &targets);
	if (error != 0)
		return (error);
	error = alias_set_intern(collection, NULL, 0, &aliases);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, locks, visibility, targets,
	    aliases, NULL, competition, result, existed));
}

/*
 * Return the destination function's canonical copy of the interprocedural
 * parts of an existing semantic state.  Stored callback targets remain local
 * to their function; demanded entries are projected separately after import.
 */
int
context_state_import(struct function_info *function,
    const struct semantic_state *source, struct semantic_state **result,
    bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_lock_set *locks;
	struct semantic_visibility_set *visibility;
	struct semantic_target_set *targets;
	struct semantic_alias_set *aliases;
	int error;

	if (source == NULL)
		return (EINVAL);
	error = lock_set_intern(collection, source->locks->entries,
	    source->locks->count, &locks);
	if (error != 0)
		return (error);
	error = visibility_set_intern(collection, source->visibility->entries,
	    source->visibility->count, &visibility);
	if (error != 0)
		return (error);
	error = target_set_intern(collection, NULL, 0, &targets);
	if (error != 0)
		return (error);
	error = alias_set_intern(collection, NULL, 0, &aliases);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, locks, visibility, targets,
	    aliases, source->operation_profile, source->competition, result,
	    existed));
}

/*
 * Map a callee exit back into the function which owns caller_state.  Callee
 * entry must equal caller state in the interprocedural dimensions.  Target
 * maps are function-local and may contain projected callee demands, so they
 * are neither compared nor returned.  Operation-family selection must match
 * at entry and the callee's exit selection is returned.  Changes to inherited
 * locks always pass through; newly held locks pass only when the callback says
 * their identities remain meaningful to the caller.  A visibility mapper
 * returns the callee's exact exit set in caller coordinates.  Without one,
 * visibility remains at its pre-call value.
 */
int
context_state_map_exit(struct function_info *function,
    const struct semantic_state *caller_state,
    const struct semantic_state *callee_entry,
    const struct semantic_state *callee_exit,
    context_lock_map_f map_new, void *lock_data,
    context_visibility_map_f map_visibility, void *visibility_data,
    struct semantic_state **result, bool *existed)
{
	struct mapped_visibility_entry {
		struct semantic_visibility_state state;
		bool changed;
	};
	struct function_context_collection *collection = &function->contexts;
	struct semantic_lock_state entries[LOCKLINT_MAX_TRACKED_LOCKS];
	struct mapped_visibility_entry mapped[LOCKLINT_MAX_TRACKED_VISIBILITY];
	struct semantic_visibility_state
	    visibility_entries[LOCKLINT_MAX_TRACKED_VISIBILITY];
	const struct semantic_lock_set *entry_locks;
	const struct semantic_lock_set *exit_locks;
	struct semantic_lock_set *locks;
	struct semantic_visibility_set *visibility;
	size_t entry_index = 0;
	size_t exit_index;
	size_t result_count = 0;
	size_t visibility_count = 0;
	int error;

	if (caller_state == NULL || callee_entry == NULL || callee_exit == NULL)
		return (EINVAL);
	if (compare_lock_set(caller_state->locks, callee_entry->locks) != 0 ||
	    compare_visibility_set(caller_state->visibility,
	    callee_entry->visibility) != 0 ||
	    caller_state->operation_profile !=
	    callee_entry->operation_profile ||
	    compare_competition(&caller_state->competition,
	    &callee_entry->competition) != 0)
		return (EINVAL);
	entry_locks = callee_entry->locks;
	exit_locks = callee_exit->locks;
	for (exit_index = 0; exit_index < exit_locks->count; exit_index++) {
		const struct semantic_lock_state *exit_lock =
		    &exit_locks->entries[exit_index];
		const struct lock_identity *mapped_lock = exit_lock->lock;
		size_t insert;
		bool inherited;

		while (entry_index < entry_locks->count &&
		    compare_lock_identity(
		    entry_locks->entries[entry_index].lock,
		    exit_lock->lock) < 0)
			entry_index++;
		inherited = entry_index < entry_locks->count &&
		    entry_locks->entries[entry_index].lock == exit_lock->lock;
		if (!inherited &&
		    (map_new == NULL ||
		    !map_new(exit_lock->lock, &mapped_lock, lock_data)))
			continue;
		for (insert = 0; insert < result_count; insert++) {
			int order = compare_lock_identity(entries[insert].lock,
			    mapped_lock);

			if (order >= 0)
				break;
		}
		if (insert < result_count &&
		    entries[insert].lock == mapped_lock) {
			entries[insert].modes |= exit_lock->modes;
			continue;
		}
		if (result_count == LOCKLINT_MAX_TRACKED_LOCKS)
			return (E2BIG);
		(void) memmove(&entries[insert + 1], &entries[insert],
		    (result_count - insert) * sizeof (entries[0]));
		entries[insert] = (struct semantic_lock_state) {
			.lock = mapped_lock,
			.modes = exit_lock->modes
		};
		result_count++;
	}
	error = lock_set_intern(collection, entries, result_count, &locks);
	if (error != 0)
		return (error);
	if (map_visibility == NULL) {
		error = visibility_set_intern(collection,
		    caller_state->visibility->entries,
		    caller_state->visibility->count, &visibility);
		if (error != 0)
			return (error);
		return (semantic_state_intern(collection, locks, visibility,
		    caller_state->targets, caller_state->aliases,
		    callee_exit->operation_profile, callee_exit->competition,
		    result, existed));
	}
	for (exit_index = 0; exit_index < callee_exit->visibility->count;
	    exit_index++) {
		const struct semantic_visibility_state *source =
		    &callee_exit->visibility->entries[exit_index];
		struct visibility_region region = source->region;
		enum semantic_visibility entry_visibility;
		bool changed;
		size_t insert;

		if (!map_visibility(&source->region, &region, visibility_data))
			continue;
		if (!visibility_region_valid(&region))
			return (EINVAL);
		changed = !context_state_visibility(callee_entry,
		    source->region, &entry_visibility) ||
		    entry_visibility != source->visibility;
		for (insert = 0; insert < visibility_count; insert++) {
			int order = compare_visibility_region(
			    &mapped[insert].state.region, &region);

			if (order >= 0)
				break;
		}
		if (insert < visibility_count &&
		    compare_visibility_region(&mapped[insert].state.region,
		    &region) == 0) {
			if (changed && !mapped[insert].changed) {
				mapped[insert].state.region = region;
				mapped[insert].state.visibility =
				    source->visibility;
				mapped[insert].changed = true;
			}
			continue;
		}
		if (visibility_count == LOCKLINT_MAX_TRACKED_VISIBILITY)
			return (E2BIG);
		(void) memmove(&mapped[insert + 1], &mapped[insert],
		    (visibility_count - insert) * sizeof (mapped[0]));
		mapped[insert].state.region = region;
		mapped[insert].state.visibility = source->visibility;
		mapped[insert].changed = changed;
		visibility_count++;
	}
	for (exit_index = 0; exit_index < visibility_count; exit_index++)
		visibility_entries[exit_index] = mapped[exit_index].state;
	error = visibility_set_intern(collection, visibility_entries,
	    visibility_count, &visibility);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, locks, visibility,
	    caller_state->targets, caller_state->aliases,
	    callee_exit->operation_profile, callee_exit->competition, result,
	    existed));
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
		return (semantic_state_intern(collection, current_locks,
		    current->visibility, current->targets, current->aliases,
		    current->operation_profile, current->competition, result,
		    existed));
	}
	if (!found && modes == 0) {
		return (semantic_state_intern(collection, current_locks,
		    current->visibility, current->targets, current->aliases,
		    current->operation_profile, current->competition, result,
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
	return (semantic_state_intern(collection, locks, current->visibility,
	    current->targets, current->aliases, current->operation_profile,
	    current->competition, result, existed));
}

int
context_state_set_visibility(struct function_info *function,
    const struct semantic_state *current, struct visibility_region region,
    enum semantic_visibility value, struct semantic_state **result,
    bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_visibility_state
	    entries[LOCKLINT_MAX_TRACKED_VISIBILITY];
	const struct semantic_visibility_set *current_visibility;
	struct semantic_visibility_set *visibility;
	size_t current_index;
	size_t result_index = 0;
	size_t result_count;
	bool inserted = false;
	int error;

	if (current == NULL || !visibility_region_valid(&region) ||
	    (value != SEMANTIC_VISIBILITY_VISIBLE &&
	    value != SEMANTIC_VISIBILITY_INVISIBLE))
		return (EINVAL);
	current_visibility = current->visibility;
	result_count = 1;
	for (current_index = 0; current_index < current_visibility->count;
	    current_index++) {
		if (!visibility_region_subsumes(&region,
		    &current_visibility->entries[current_index].region))
			result_count++;
	}
	if (result_count > LOCKLINT_MAX_TRACKED_VISIBILITY)
		return (E2BIG);
	for (current_index = 0; current_index < current_visibility->count;
	    current_index++) {
		const struct semantic_visibility_state *entry =
		    &current_visibility->entries[current_index];

		if (visibility_region_subsumes(&region, &entry->region))
			continue;
		if (!inserted &&
		    compare_visibility_region(&region, &entry->region) < 0) {
			entries[result_index].region = region;
			entries[result_index].visibility = value;
			result_index++;
			inserted = true;
		}
		entries[result_index++] = *entry;
	}
	if (!inserted) {
		entries[result_index].region = region;
		entries[result_index].visibility = value;
		result_index++;
	}
	if (result_index != result_count)
		abort();
	error = visibility_set_intern(collection, entries, result_count,
	    &visibility);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, current->locks, visibility,
	    current->targets, current->aliases, current->operation_profile,
	    current->competition, result, existed));
}

int
context_state_set_targets(struct function_info *function,
    const struct semantic_state *current, struct visibility_region region,
    const struct call_target_set *value, struct semantic_state **result,
    bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_target_state entries[LOCKLINT_MAX_TRACKED_TARGETS];
	const struct semantic_target_set *current_targets;
	struct semantic_target_set *targets;
	size_t current_index;
	size_t result_index = 0;
	bool inserted = false;
	int error;

	if (current == NULL || !visibility_region_valid(&region))
		return (EINVAL);
	current_targets = current->targets;
	for (current_index = 0; current_index < current_targets->count;
	    current_index++) {
		const struct semantic_target_state *entry =
		    &current_targets->entries[current_index];
		bool overlap;

		overlap = region.analysis_object ==
		    entry->region.analysis_object &&
		    region.target_offset <= entry->region.target_offset +
		    (int64_t)entry->region.target_length - 1 &&
		    entry->region.target_offset <= region.target_offset +
		    (int64_t)region.target_length - 1;
		if (overlap)
			continue;
		if (!inserted && value != NULL &&
		    compare_visibility_region(&region, &entry->region) < 0) {
			entries[result_index++] = (struct semantic_target_state) {
				.region = region,
				.targets = value
			};
			inserted = true;
		}
		if (result_index == LOCKLINT_MAX_TRACKED_TARGETS)
			return (E2BIG);
		entries[result_index++] = *entry;
	}
	if (!inserted && value != NULL) {
		if (result_index == LOCKLINT_MAX_TRACKED_TARGETS)
			return (E2BIG);
		entries[result_index++] = (struct semantic_target_state) {
			.region = region,
			.targets = value
		};
	}
	error = target_set_intern(collection, entries, result_index, &targets);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, current->locks,
	    current->visibility, targets, current->aliases,
	    current->operation_profile,
	    current->competition, result, existed));
}

/*
 * Discard function-local stored callback knowledge before crossing a call.
 * The current call may use that knowledge for target and argument resolution,
 * but an arbitrary callee may mutate storage which it can reach.
 */
int
context_state_clear_targets(struct function_info *function,
    const struct semantic_state *current, struct semantic_state **result,
    bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_target_set *targets;
	int error;

	if (current == NULL)
		return (EINVAL);
	error = target_set_intern(collection, NULL, 0, &targets);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, current->locks,
	    current->visibility, targets, current->aliases,
	    current->operation_profile,
	    current->competition, result, existed));
}

/*
 * Bind one function-local value identity to an existing canonical object.
 * A PHI target may be replaced while traversing a different predecessor.
 */
int
context_state_set_alias(struct function_info *function,
    const struct semantic_state *current, const void *source,
    const struct lock_identity *target, struct semantic_state **result,
    bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_alias_state entries[LOCKLINT_MAX_TRACKED_ALIASES];
	const struct semantic_alias_set *current_aliases;
	struct semantic_alias_set *aliases;
	size_t insert;
	size_t result_count;
	int error;

	if (current == NULL || source == NULL || target == NULL)
		return (EINVAL);
	current_aliases = current->aliases;
	for (insert = 0; insert < current_aliases->count; insert++) {
		int order = AVL_PCMP(current_aliases->entries[insert].source,
		    source);

		if (order >= 0)
			break;
	}
	if (insert < current_aliases->count &&
	    current_aliases->entries[insert].source == source) {
		if (current_aliases->entries[insert].target == target) {
			return (semantic_state_intern(collection, current->locks,
			    current->visibility, current->targets,
			    current->aliases, current->operation_profile,
			    current->competition, result, existed));
		}
		(void) memcpy(entries, current_aliases->entries,
		    current_aliases->count * sizeof (entries[0]));
		entries[insert].target = target;
		error = alias_set_intern(collection, entries,
		    current_aliases->count, &aliases);
		if (error != 0)
			return (error);
		return (semantic_state_intern(collection, current->locks,
		    current->visibility, current->targets, aliases,
		    current->operation_profile, current->competition, result,
		    existed));
	}
	if (current_aliases->count == LOCKLINT_MAX_TRACKED_ALIASES)
		return (E2BIG);
	result_count = current_aliases->count + 1;
	if (insert != 0)
		(void) memcpy(entries, current_aliases->entries,
		    insert * sizeof (entries[0]));
	entries[insert] = (struct semantic_alias_state) {
		.source = source,
		.target = target
	};
	if (insert < current_aliases->count) {
		(void) memcpy(&entries[insert + 1],
		    &current_aliases->entries[insert],
		    (current_aliases->count - insert) * sizeof (entries[0]));
	}
	error = alias_set_intern(collection, entries, result_count, &aliases);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, current->locks,
	    current->visibility, current->targets, aliases,
	    current->operation_profile, current->competition, result, existed));
}

int
context_state_remove_alias(struct function_info *function,
    const struct semantic_state *current, const void *source,
    struct semantic_state **result, bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct semantic_alias_state entries[LOCKLINT_MAX_TRACKED_ALIASES];
	const struct semantic_alias_set *current_aliases;
	struct semantic_alias_set *aliases;
	size_t index;
	size_t insert;
	int error;

	if (current == NULL || source == NULL)
		return (EINVAL);
	current_aliases = current->aliases;
	for (index = 0; index < current_aliases->count; index++) {
		if (current_aliases->entries[index].source == source)
			break;
	}
	if (index == current_aliases->count) {
		return (semantic_state_intern(collection, current->locks,
		    current->visibility, current->targets, current->aliases,
		    current->operation_profile, current->competition, result,
		    existed));
	}
	for (insert = 0; insert < index; insert++)
		entries[insert] = current_aliases->entries[insert];
	for (insert = index + 1; insert < current_aliases->count; insert++)
		entries[insert - 1] = current_aliases->entries[insert];
	error = alias_set_intern(collection, entries,
	    current_aliases->count - 1, &aliases);
	if (error != 0)
		return (error);
	return (semantic_state_intern(collection, current->locks,
	    current->visibility, current->targets, aliases,
	    current->operation_profile, current->competition, result, existed));
}

const struct lock_identity *
context_state_alias(const struct semantic_state *state, const void *source)
{
	size_t low = 0;
	size_t high;

	if (state == NULL || source == NULL)
		return (NULL);
	high = state->aliases->count;
	while (low < high) {
		size_t middle = low + (high - low) / 2;
		const struct semantic_alias_state *entry =
		    &state->aliases->entries[middle];
		int order = AVL_PCMP(entry->source, source);

		if (order < 0)
			low = middle + 1;
		else if (order > 0)
			high = middle;
		else
			return (entry->target);
	}
	return (NULL);
}

size_t
context_state_alias_count(const struct semantic_state *state)
{
	return (state->aliases->count);
}

/*
 * Select one operation family for the remainder of this semantic path.
 * Repeating the same selection reuses the state; replacing it is invalid.
 */
int
context_state_select_operation_profile(struct function_info *function,
    const struct semantic_state *current,
    const struct operation_family_profile *profile,
    struct semantic_state **result, bool *existed)
{
	if (current == NULL || profile == NULL)
		return (EINVAL);
	if (current->operation_profile != NULL &&
	    current->operation_profile != profile)
		return (EEXIST);
	return (semantic_state_intern(&function->contexts, current->locks,
	    current->visibility, current->targets, current->aliases, profile,
	    current->competition, result, existed));
}

const struct operation_family_profile *
context_state_operation_profile(const struct semantic_state *state)
{
	return (state != NULL ? state->operation_profile : NULL);
}

const struct call_target_set *
context_state_targets(const struct semantic_state *state,
    struct visibility_region region)
{
	size_t low = 0;
	size_t high;

	if (state == NULL || !visibility_region_valid(&region))
		return (NULL);
	high = state->targets->count;
	while (low < high) {
		size_t middle = low + (high - low) / 2;
		const struct semantic_target_state *entry =
		    &state->targets->entries[middle];
		int order = compare_visibility_region(&entry->region, &region);

		if (order < 0)
			low = middle + 1;
		else if (order > 0)
			high = middle;
		else
			return (entry->targets);
	}
	return (NULL);
}

bool
context_state_targets_overlap(const struct semantic_state *state,
    struct visibility_region region)
{
	size_t index;

	if (state == NULL || !visibility_region_valid(&region))
		return (false);
	for (index = 0; index < state->targets->count; index++) {
		const struct visibility_region *entry =
		    &state->targets->entries[index].region;

		if (region.analysis_object == entry->analysis_object &&
		    region.target_offset <= entry->target_offset +
		    (int64_t)entry->target_length - 1 &&
		    entry->target_offset <= region.target_offset +
		    (int64_t)region.target_length - 1)
			return (true);
	}
	return (false);
}

int
context_state_set_competition(struct function_info *function,
    const struct semantic_state *current,
    struct competition_interval competition,
    struct semantic_state **result, bool *existed)
{
	if (current == NULL)
		return (EINVAL);
	if (!competition.minimum_unbounded &&
	    !competition.maximum_unbounded &&
	    competition.minimum > competition.maximum)
		return (EINVAL);
	if (competition.entry_condition &&
	    (competition.minimum_unbounded ||
	    competition.maximum_unbounded ||
	    competition.minimum != 0 || competition.maximum != 1))
		return (EINVAL);
	if (competition.minimum_unbounded)
		competition.minimum = 0;
	if (competition.maximum_unbounded)
		competition.maximum = 0;
	return (semantic_state_intern(&function->contexts, current->locks,
	    current->visibility, current->targets, current->aliases,
	    current->operation_profile, competition, result, existed));
}

/*
 * Shift an ordinary competition interval by one level.  An unresolved entry
 * condition instead resolves to the level selected by its first transition.
 */
int
context_competition_adjust(struct competition_interval competition,
    int adjustment, struct competition_interval *result)
{
	if (result == NULL || (adjustment != -1 && adjustment != 1))
		return (EINVAL);
	if (competition.entry_condition) {
		competition.minimum = adjustment > 0 ? 1 : 0;
		competition.maximum = competition.minimum;
		competition.entry_condition = false;
	} else {
		if ((!competition.minimum_unbounded &&
		    ((adjustment > 0 && competition.minimum == INT64_MAX) ||
		    (adjustment < 0 && competition.minimum == INT64_MIN))) ||
		    (!competition.maximum_unbounded &&
		    ((adjustment > 0 && competition.maximum == INT64_MAX) ||
		    (adjustment < 0 && competition.maximum == INT64_MIN))))
			return (EOVERFLOW);
		if (!competition.minimum_unbounded)
			competition.minimum += adjustment;
		if (!competition.maximum_unbounded)
			competition.maximum += adjustment;
	}
	*result = competition;
	return (0);
}

int
context_state_adjust_competition(struct function_info *function,
    const struct semantic_state *current, int adjustment,
    struct semantic_state **result, bool *existed)
{
	struct competition_interval competition;
	int error;

	if (current == NULL)
		return (EINVAL);
	error = context_competition_adjust(current->competition, adjustment,
	    &competition);
	if (error != 0)
		return (error);
	return (context_state_set_competition(function, current, competition,
	    result, existed));
}

bool
context_state_competition_contains(const struct semantic_state *outer,
    const struct semantic_state *inner)
{
	const struct competition_interval *left;
	const struct competition_interval *right;

	if (outer == NULL || inner == NULL || outer->locks != inner->locks ||
	    outer->visibility != inner->visibility ||
	    outer->targets != inner->targets)
		return (false);
	left = &outer->competition;
	right = &inner->competition;
	if (left->entry_condition || right->entry_condition)
		return (compare_competition(left, right) == 0);
	if (!left->minimum_unbounded &&
	    (right->minimum_unbounded || left->minimum > right->minimum))
		return (false);
	if (!left->maximum_unbounded &&
	    (right->maximum_unbounded || left->maximum < right->maximum))
		return (false);
	return (true);
}

/*
 * Join equal-lock states by competition interval.  Widening makes an endpoint
 * unbounded only when the incoming join extends beyond the prior interval.
 */
int
context_state_merge_competition(struct function_info *function,
    const struct semantic_state *prior, const struct semantic_state *incoming,
    bool widen, struct semantic_state **result, bool *existed)
{
	struct competition_interval joined;
	const struct competition_interval *left;
	const struct competition_interval *right;

	if (prior == NULL || incoming == NULL || prior->locks != incoming->locks ||
	    prior->visibility != incoming->visibility ||
	    prior->targets != incoming->targets ||
	    prior->operation_profile != incoming->operation_profile)
		return (EINVAL);
	left = &prior->competition;
	right = &incoming->competition;
	joined.minimum_unbounded =
	    left->minimum_unbounded || right->minimum_unbounded;
	joined.maximum_unbounded =
	    left->maximum_unbounded || right->maximum_unbounded;
	joined.minimum = joined.minimum_unbounded ? 0 :
	    (left->minimum < right->minimum ? left->minimum : right->minimum);
	joined.maximum = joined.maximum_unbounded ? 0 :
	    (left->maximum > right->maximum ? left->maximum : right->maximum);
	joined.entry_condition =
	    left->entry_condition && right->entry_condition;
	if (widen && !left->minimum_unbounded &&
	    (joined.minimum_unbounded || joined.minimum < left->minimum)) {
		joined.minimum = 0;
		joined.minimum_unbounded = true;
	}
	if (widen && !left->maximum_unbounded &&
	    (joined.maximum_unbounded || joined.maximum > left->maximum)) {
		joined.maximum = 0;
		joined.maximum_unbounded = true;
	}
	return (context_state_set_competition(function, prior, joined, result,
	    existed));
}

/*
 * Find or create the context identified by canonical bindings and entry
 * state.  Allocation failure leaves both output arguments unchanged.
 */
static int
context_create_impl(struct function_info *function,
    const struct binding_environment *bindings,
    const struct semantic_state *entry_state,
    enum function_context_kind kind, struct function_context **result,
    bool *existed)
{
	struct function_context_collection *collection = &function->contexts;
	struct function_context key = {
		.function = function,
		.bindings = bindings,
		.entry_state = entry_state,
		.kind = kind
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
	context->kind = kind;
	avl_create(&context->point_states, compare_point_state,
	    sizeof (struct point_state), offsetof(struct point_state, by_key));
	dependency_records_create(context);
	provenance_edges_create(context);
	avl_insert(&collection->contexts, context, where);
	*result = context;
	*existed = false;
	return (0);
}

int
context_create(struct function_info *function,
    const struct binding_environment *bindings,
    const struct semantic_state *entry_state,
    struct function_context **result, bool *existed)
{
	return (context_create_impl(function, bindings, entry_state,
	    FUNCTION_CONTEXT_CALLER, result, existed));
}

int
context_root_create(struct function_info *function,
    const struct binding_environment *bindings,
    const struct semantic_state *entry_state,
    struct function_context **result, bool *existed)
{
	return (context_create_impl(function, bindings, entry_state,
	    FUNCTION_CONTEXT_ROOT, result, existed));
}

int
context_effect_create(struct function_info *function,
    const struct binding_environment *bindings,
    const struct semantic_state *entry_state,
    struct function_context **result, bool *existed)
{
	return (context_create_impl(function, bindings, entry_state,
	    FUNCTION_CONTEXT_EFFECT_CALLER, result, existed));
}

int
context_effect_contract_create(struct function_info *function,
    const struct binding_environment *bindings,
    const struct semantic_state *entry_state,
    struct function_context **result, bool *existed)
{
	return (context_create_impl(function, bindings, entry_state,
	    FUNCTION_CONTEXT_EFFECT_CONTRACT, result, existed));
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

/*
 * Record a state arriving on a CFG back edge.  Existing equal-lock states at
 * the point form the prior interval; covered arrivals stop, while extensions
 * widen before ordinary immutable point-state insertion.
 */
int
context_point_state_record_widened(struct function_context *context,
    struct analysis_point point, const struct semantic_state *state,
    struct point_state **result, bool *existed, bool *widened)
{
	struct point_state key = {
		.context = context,
		.point = point
	};
	struct point_state *point_state;
	const struct semantic_state *prior = NULL;
	struct semantic_state *merged;
	avl_index_t where;
	bool state_existed;
	int error;

	if (context == NULL || state == NULL)
		return (EINVAL);
	statistics.backedge_point_states_find++;
	(void) avl_find(&context->point_states, &key, &where);
	statistics.backedge_point_states_enum++;
	for (point_state = avl_nearest(&context->point_states, where, AVL_AFTER);
	    point_state != NULL &&
	    point_state->point.block == point.block &&
	    point_state->point.next_instruction == point.next_instruction &&
	    point_state->point.conditional_instruction ==
	    point.conditional_instruction &&
	    point_state->point.conditional_nonzero ==
	    point.conditional_nonzero;
	    point_state = AVL_NEXT(&context->point_states, point_state)) {
		if (point_state->state->locks != state->locks ||
		    point_state->state->visibility != state->visibility ||
		    point_state->state->targets != state->targets)
			continue;
		if (context_state_competition_contains(point_state->state,
		    state)) {
			*result = point_state;
			*existed = true;
			*widened = false;
			return (0);
		}
		if (prior == NULL) {
			prior = point_state->state;
			continue;
		}
		statistics.backedge_widening_semantic_states_find++;
		error = context_state_merge_competition(context->function, prior,
		    point_state->state, false, &merged, &state_existed);
		if (error != 0)
			return (error);
		prior = merged;
	}
	if (prior == NULL) {
		statistics.backedge_record_point_states_find++;
		error = context_point_state_record(context, point, state, result,
		    existed);
	} else {
		statistics.backedge_widening_semantic_states_find++;
		error = context_state_merge_competition(context->function, prior,
		    state, true, &merged, &state_existed);
		if (error == 0) {
			statistics.backedge_record_point_states_find++;
			error = context_point_state_record(context, point, merged,
			    result, existed);
		}
	}
	if (error != 0)
		return (error);
	*widened = prior != NULL;
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
context_visibility_set_count(struct function_info *function)
{
	return (avl_numnodes(&function->contexts.visibility_sets));
}

size_t
context_target_set_count(struct function_info *function)
{
	return (avl_numnodes(&function->contexts.target_sets));
}

size_t
context_visibility_sets_created(struct function_info *function)
{
	return (function->contexts.visibility_sets_created);
}

size_t
context_visibility_sets_reused(struct function_info *function)
{
	return (function->contexts.visibility_sets_reused);
}

size_t
context_state_lock_count(const struct semantic_state *state)
{
	return (state->locks->count);
}

/*
 * Enumerate the immutable lock set without exposing its representation.
 */
bool
context_state_lock_at(const struct semantic_state *state, size_t index,
    const struct lock_identity **lock, unsigned int *modes)
{
	if (index >= state->locks->count)
		return (false);
	*lock = state->locks->entries[index].lock;
	*modes = state->locks->entries[index].modes;
	return (true);
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

/*
 * Combine modes held for exact identities reached through one canonical
 * member role.  Role conflicts on an identity match nothing.
 */
unsigned int
context_state_lock_role_modes(const struct semantic_state *state,
    const struct type_member *role)
{
	unsigned int modes = 0;
	size_t index;

	for (index = 0; index < state->locks->count; index++) {
		const struct semantic_lock_state *entry =
		    &state->locks->entries[index];

		if (!entry->lock->role_conflict && entry->lock->role == role)
			modes |= entry->modes;
	}
	return (modes);
}

size_t
context_state_visibility_count(const struct semantic_state *state)
{
	return (state->visibility->count);
}

size_t
context_state_target_count(const struct semantic_state *state)
{
	return (state->targets->count);
}

bool
context_state_visibility(const struct semantic_state *state,
    struct visibility_region region,
    enum semantic_visibility *visibility)
{
	size_t index;

	for (index = 0; index < state->visibility->count; index++) {
		int order = compare_visibility_region(
		    &state->visibility->entries[index].region, &region);

		if (order == 0) {
			if (visibility != NULL)
				*visibility =
				    state->visibility->entries[index].visibility;
			return (true);
		}
		if (order > 0)
			break;
	}
	return (false);
}

/*
 * Find the narrowest stored region containing an access.  The flat set is
 * intentionally small, and scanning it avoids a second index while preserving
 * deterministic most-specific override semantics.
 */
bool
context_state_effective_visibility(const struct semantic_state *state,
    struct visibility_region region,
    enum semantic_visibility *visibility)
{
	const struct semantic_visibility_state *best = NULL;
	size_t index;

	if (visibility != NULL)
		*visibility = SEMANTIC_VISIBILITY_VISIBLE;
	if (state == NULL || !visibility_region_valid(&region))
		return (false);
	for (index = 0; index < state->visibility->count; index++) {
		const struct semantic_visibility_state *entry =
		    &state->visibility->entries[index];

		if (!visibility_region_subsumes(&entry->region, &region))
			continue;
		if (best == NULL ||
		    entry->region.target_length < best->region.target_length)
			best = entry;
	}
	if (best == NULL)
		return (false);
	if (visibility != NULL)
		*visibility = best->visibility;
	return (true);
}

struct competition_interval
context_state_competition(const struct semantic_state *state)
{
	return (state->competition);
}

size_t
context_point_state_count(struct function_context *context)
{
	return (avl_numnodes(&context->point_states));
}
