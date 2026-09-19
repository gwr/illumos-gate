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

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "context.h"
#include "dependency.h"
#include "function_info.h"
#include "provenance.h"

static unsigned int failures;

#define	REGION(object, offset, length)	((struct visibility_region) { \
	.analysis_object = (object), \
	.target_offset = (offset), \
	.target_length = (length) \
})

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

void
provenance_edges_create(struct function_context *context)
{
	(void) context;
}

void
provenance_edges_free(struct function_context *context)
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

static bool
include_selected_lock(const struct lock_identity *lock, void *data)
{
	return (lock == data);
}

struct test_region_mapping {
	const void *from;
	const void *to;
};

static bool
map_selected_region(const struct visibility_region *source,
    struct visibility_region *result, void *data)
{
	const struct test_region_mapping *mapping = data;

	*result = *source;
	if (source->analysis_object == mapping->from)
		result->analysis_object = mapping->to;
	return (true);
}

static bool
copy_region(const struct visibility_region *source,
    struct visibility_region *result, void *data)
{
	(void) data;
	*result = *source;
	return (true);
}

static void
test_exit_state_mapping(void)
{
	struct function_info caller = { 0 };
	struct function_info callee = { 0 };
	unsigned int identities[2];
	const struct lock_identity *first =
	    (const struct lock_identity *)&identities[0];
	const struct lock_identity *second =
	    (const struct lock_identity *)&identities[1];
	struct semantic_state *caller_empty;
	struct semantic_state *caller_held;
	struct semantic_state *callee_entry;
	struct semantic_state *callee_empty;
	struct semantic_state *callee_exit;
	struct semantic_state *callee_deep;
	struct semantic_state *caller_visibility;
	struct semantic_state *callee_visibility_entry;
	struct semantic_state *callee_visibility_exit;
	struct semantic_state *mapped;
	struct test_region_mapping region_mapping = {
		.from = second,
		.to = first
	};
	enum semantic_visibility visibility;
	bool existed;
	int error;

	context_collection_create(&caller);
	context_collection_create(&callee);
	error = context_empty_state_intern(&caller, &caller_empty, &existed);
	check(error == 0, "create exit-map caller empty state");
	error = context_state_set_lock(&caller, caller_empty, first, 1,
	    &caller_held, &existed);
	check(error == 0, "create exit-map caller held state");
	error = context_state_import(&callee, caller_held, &callee_entry,
	    &existed);
	check(error == 0, "import exit-map callee entry");
	error = context_state_set_lock(&callee, callee_entry, first, 0,
	    &callee_empty, &existed);
	check(error == 0, "release inherited lock in callee");
	error = context_state_set_lock(&callee, callee_empty, second, 2,
	    &callee_exit, &existed);
	check(error == 0, "acquire new lock in callee");

	error = context_state_map_exit(&caller, caller_held, callee_entry,
	    callee_exit, NULL, NULL, NULL, NULL, &mapped, &existed);
	check(error == 0 && existed && mapped == caller_empty,
	    "exit mapping removes released inherited lock");
	check(context_state_lock_modes(mapped, second) == 0,
	    "exit mapping filters unapproved new lock");
	error = context_state_map_exit(&caller, caller_held, callee_entry,
	    callee_exit, include_selected_lock, (void *)second, NULL, NULL,
	    &mapped, &existed);
	check(error == 0 && !existed,
	    "exit mapping includes approved new lock");
	check(context_state_lock_modes(mapped, first) == 0 &&
	    context_state_lock_modes(mapped, second) == 2,
	    "exit mapping preserves complete mapped state");
	error = context_state_set_competition(&callee, callee_exit,
	    (struct competition_interval){ .minimum = 2, .maximum = 2 },
	    &callee_deep, &existed);
	check(error == 0 && !existed, "create deep callee exit state");
	error = context_state_map_exit(&caller, caller_held, callee_entry,
	    callee_deep, include_selected_lock, (void *)second, NULL, NULL,
	    &mapped, &existed);
	check(error == 0 && !existed &&
	    context_state_competition(mapped).minimum == 2 &&
	    context_state_competition(mapped).maximum == 2,
	    "exit mapping preserves competition depth");
	error = context_state_set_visibility(&caller, caller_empty,
	    REGION(first, 0, 4),
	    SEMANTIC_VISIBILITY_INVISIBLE, &caller_visibility, &existed);
	check(error == 0, "create exit-map caller visibility");
	error = context_state_import(&callee, caller_visibility,
	    &callee_visibility_entry, &existed);
	check(error == 0, "import exit-map callee visibility entry");
	error = context_state_set_visibility(&callee, callee_visibility_entry,
	    REGION(second, 0, 4), SEMANTIC_VISIBILITY_VISIBLE,
	    &callee_visibility_exit,
	    &existed);
	check(error == 0, "change visibility in callee");
	error = context_state_map_exit(&caller, caller_visibility,
	    callee_visibility_entry, callee_visibility_exit, NULL, NULL,
	    copy_region, NULL, &mapped, &existed);
	check(error == 0 &&
	    context_state_visibility(mapped, REGION(first, 0, 4), &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_INVISIBLE &&
	    context_state_visibility(mapped, REGION(second, 0, 4),
	    &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "exit mapping preserves complete visibility state");
	error = context_state_map_exit(&caller, caller_visibility,
	    callee_visibility_entry, callee_visibility_exit, NULL, NULL,
	    NULL, NULL, &mapped, &existed);
	check(error == 0 && context_state_visibility_count(mapped) == 1 &&
	    context_state_visibility(mapped, REGION(first, 0, 4),
	    &visibility) && visibility == SEMANTIC_VISIBILITY_INVISIBLE,
	    "disabled exit mapping preserves caller visibility");
	error = context_state_map_exit(&caller, caller_visibility,
	    callee_visibility_entry, callee_visibility_exit, NULL, NULL,
	    map_selected_region, &region_mapping, &mapped, &existed);
	check(error == 0 && context_state_visibility_count(mapped) == 1 &&
	    context_state_visibility(mapped, REGION(first, 0, 4),
	    &visibility) && visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "changed mapped visibility overrides inherited visibility");

	context_collection_free(&callee);
	context_collection_free(&caller);
}

static void
test_state_interning(void)
{
	struct function_info function = { 0 };
	struct semantic_state *state;
	struct semantic_state *entry;
	struct semantic_state *same;
	struct competition_interval competition;
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
	error = context_entry_state_intern(&function, &entry, &existed);
	competition = context_state_competition(entry);
	check(error == 0 && !existed && entry != state,
	    "entry state is distinct from empty state");
	check(competition.minimum == 0 && competition.maximum == 1 &&
	    competition.entry_condition,
	    "entry state has unresolved entry condition");
	error = context_entry_state_intern(&function, &same, &existed);
	check(error == 0 && existed && same == entry,
	    "entry semantic state is interned");

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
test_competition_depth_interning(void)
{
	struct function_info first = { 0 };
	struct function_info second = { 0 };
	struct semantic_state *empty;
	struct semantic_state *competing;
	struct semantic_state *ranged;
	struct semantic_state *unbounded;
	struct semantic_state *ordinary_entry_range;
	struct semantic_state *entry_condition;
	struct semantic_state *adjusted;
	struct semantic_state *upper_unbounded;
	struct semantic_state *widened;
	struct semantic_state *held;
	struct semantic_state *visible;
	unsigned int identity;
	struct semantic_state *same;
	struct semantic_state *imported;
	struct semantic_state *unchanged = NULL;
	struct competition_interval competition;
	bool existed;
	int error;

	context_collection_create(&first);
	context_collection_create(&second);
	error = context_empty_state_intern(&first, &empty, &existed);
	competition = context_state_competition(empty);
	check(error == 0 && competition.minimum == 0 &&
	    competition.maximum == 0 && !competition.minimum_unbounded &&
	    !competition.maximum_unbounded,
	    "empty state has zero competition depth");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){ .minimum = 1, .maximum = 1 },
	    &competing, &existed);
	check(error == 0 && !existed && competing != empty,
	    "competition depth creates distinct state");
	check(competing->locks == empty->locks,
	    "competition states share canonical lock set");
	error = context_state_set_competition(&first, competing,
	    (struct competition_interval){ .minimum = 1, .maximum = 1 },
	    &same, &existed);
	check(error == 0 && existed && same == competing,
	    "unchanged competition depth reuses state");
	error = context_state_import(&second, competing, &imported, &existed);
	competition = context_state_competition(imported);
	check(error == 0 && !existed && competition.minimum == 1 &&
	    competition.maximum == 1,
	    "state import preserves competition depth");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){ .minimum = -1, .maximum = 2 },
	    &ranged, &existed);
	check(error == 0 && !existed,
	    "signed competition range creates distinct state");
	competition = context_state_competition(ranged);
	check(competition.minimum == -1 && competition.maximum == 2,
	    "signed competition range is preserved");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){
	    .minimum = -7, .maximum = 9,
	    .minimum_unbounded = true, .maximum_unbounded = true },
	    &unbounded, &existed);
	competition = context_state_competition(unbounded);
	check(error == 0 && !existed && competition.minimum_unbounded &&
	    competition.maximum_unbounded && competition.minimum == 0 &&
	    competition.maximum == 0,
	    "unbounded endpoints have one canonical representation");
	existed = true;
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){ .minimum = 2, .maximum = 1 },
	    &unchanged, &existed);
	check(error == EINVAL && unchanged == NULL && existed,
	    "invalid competition interval preserves outputs");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){ .minimum = 0, .maximum = 1 },
	    &ordinary_entry_range, &existed);
	check(error == 0 && !existed,
	    "ordinary zero-one range creates distinct state");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){
	    .minimum = 0, .maximum = 1, .entry_condition = true },
	    &entry_condition, &existed);
	check(error == 0 && !existed &&
	    entry_condition != ordinary_entry_range,
	    "entry condition is distinct from ordinary zero-one range");
	competition = context_state_competition(entry_condition);
	check(competition.entry_condition,
	    "entry condition is preserved");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){
	    .minimum = 1, .maximum = 1, .entry_condition = true },
	    &unchanged, &existed);
	check(error == EINVAL,
	    "entry condition requires finite zero-one range");
	error = context_state_adjust_competition(&first, entry_condition, 1,
	    &adjusted, &existed);
	competition = context_state_competition(adjusted);
	check(error == 0 && existed && adjusted == competing &&
	    competition.minimum == 1 && competition.maximum == 1 &&
	    !competition.entry_condition,
	    "competition entry transition establishes exact one");
	error = context_state_adjust_competition(&first, entry_condition, -1,
	    &adjusted, &existed);
	check(error == 0 && existed && adjusted == empty,
	    "no-competition entry transition establishes exact zero");
	error = context_state_adjust_competition(&first, ranged, -1,
	    &adjusted, &existed);
	competition = context_state_competition(adjusted);
	check(error == 0 && !existed && competition.minimum == -2 &&
	    competition.maximum == 1,
	    "ordinary competition interval shifts through zero");
	error = context_state_adjust_competition(&first, unbounded, 1,
	    &adjusted, &existed);
	check(error == 0 && existed && adjusted == unbounded,
	    "unbounded competition interval is unchanged");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){
	    .minimum = INT64_MAX, .maximum = INT64_MAX },
	    &adjusted, &existed);
	check(error == 0, "create maximum competition interval");
	error = context_state_adjust_competition(&first, adjusted, 1,
	    &same, &existed);
	check(error == EOVERFLOW,
	    "competition increment overflow is rejected");
	error = context_state_adjust_competition(&first, empty, 2,
	    &same, &existed);
	check(error == EINVAL,
	    "unsupported competition adjustment is rejected");
	check(context_state_competition_contains(ordinary_entry_range, empty) &&
	    context_state_competition_contains(ordinary_entry_range,
	    competing), "ordinary range contains exact endpoints");
	check(!context_state_competition_contains(entry_condition,
	    ordinary_entry_range) &&
	    !context_state_competition_contains(ordinary_entry_range,
	    entry_condition), "entry condition is not ordinary containment");
	error = context_state_merge_competition(&first, empty, competing,
	    false, &adjusted, &existed);
	check(error == 0 && existed && adjusted == ordinary_entry_range,
	    "competition join reuses ordinary hull");
	error = context_state_merge_competition(&first, ranged, competing,
	    false, &adjusted, &existed);
	check(error == 0 && existed && adjusted == ranged,
	    "competition join reuses containing interval");
	error = context_state_merge_competition(&first, ordinary_entry_range,
	    ranged, true, &widened, &existed);
	competition = context_state_competition(widened);
	check(error == 0 && existed && widened == unbounded &&
	    competition.minimum_unbounded && competition.maximum_unbounded,
	    "competition widening opens both growing endpoints");
	error = context_state_set_competition(&first, empty,
	    (struct competition_interval){
	    .minimum = 0, .maximum_unbounded = true },
	    &upper_unbounded, &existed);
	check(error == 0 && !existed,
	    "create upper-unbounded competition interval");
	error = context_state_merge_competition(&first, upper_unbounded,
	    ranged, true, &adjusted, &existed);
	competition = context_state_competition(adjusted);
	check(error == 0 && existed && adjusted == unbounded &&
	    competition.minimum_unbounded && competition.maximum_unbounded,
	    "competition widening preserves independently unbounded endpoint");
	error = context_state_set_lock(&first, empty,
	    (const struct lock_identity *)&identity, 1, &held, &existed);
	check(error == 0, "create different-lock competition state");
	error = context_state_merge_competition(&first, empty, held, false,
	    &adjusted, &existed);
	check(error == EINVAL,
	    "competition merge rejects different lock sets");
	error = context_state_set_visibility(&first, empty,
	    REGION(&identity, 0, 4),
	    SEMANTIC_VISIBILITY_VISIBLE, &visible, &existed);
	check(error == 0, "create different-visibility competition state");
	error = context_state_merge_competition(&first, empty, visible, false,
	    &adjusted, &existed);
	check(error == EINVAL,
	    "competition merge rejects different visibility sets");

	context_collection_free(&second);
	context_collection_free(&first);
}

/*
 * Lock sets are immutable, canonical, and independent of the order in which
 * their entries were added.
 */
static void
test_lock_state_interning(void)
{
	struct function_info function = { 0 };
	unsigned int identities[2];
	const struct lock_identity *first =
	    (const struct lock_identity *)&identities[0];
	const struct lock_identity *second =
	    (const struct lock_identity *)&identities[1];
	struct semantic_state *empty;
	struct semantic_state *first_held;
	struct semantic_state *first_second;
	struct semantic_state *second_held;
	struct semantic_state *same;
	struct semantic_state *changed;
	bool existed;
	int error;

	context_collection_create(&function);
	error = context_empty_state_intern(&function, &empty, &existed);
	check(error == 0, "create empty state for lock sets");
	check(context_state_lock_count(empty) == 0, "empty state has no locks");

	error = context_state_set_lock(&function, empty, first, 1,
	    &first_held, &existed);
	check(error == 0 && !existed, "create one-lock state");
	check(context_state_lock_count(first_held) == 1,
	    "one-lock state has one lock");
	check(context_state_lock_modes(first_held, first) == 1,
	    "one-lock state records modes");
	check(context_state_lock_modes(empty, first) == 0,
	    "lock transition preserves empty state");

	error = context_state_set_lock(&function, first_held, first, 1,
	    &same, &existed);
	check(error == 0 && existed && same == first_held,
	    "unchanged lock state is reused");

	error = context_state_set_lock(&function, first_held, second, 2,
	    &first_second, &existed);
	check(error == 0 && !existed, "create two-lock state");
	error = context_state_set_lock(&function, empty, second, 2,
	    &second_held, &existed);
	check(error == 0 && !existed, "create alternate one-lock state");
	error = context_state_set_lock(&function, second_held, first, 1,
	    &same, &existed);
	check(error == 0 && existed && same == first_second,
	    "lock insertion order reuses canonical state");

	error = context_state_set_lock(&function, first_second, first, 0,
	    &same, &existed);
	check(error == 0 && existed && same == second_held,
	    "lock removal reuses canonical state");
	error = context_state_set_lock(&function, second_held, second, 4,
	    &changed, &existed);
	check(error == 0 && !existed, "create changed-mode state");
	check(context_state_lock_modes(changed, second) == 4,
	    "lock mode is replaced");
	check(context_state_lock_modes(second_held, second) == 2,
	    "mode transition preserves prior state");
	check(context_lock_set_count(&function) == 5,
	    "function owns five canonical lock sets");
	check(context_state_count(&function) == 5,
	    "function owns five canonical semantic states");

	context_collection_free(&function);
}

static void
test_lock_state_limit(void)
{
	struct function_info function = { 0 };
	unsigned int identities[LOCKLINT_MAX_TRACKED_LOCKS + 1];
	struct semantic_state *state;
	struct semantic_state *next;
	bool existed;
	size_t index;
	int error;

	context_collection_create(&function);
	error = context_empty_state_intern(&function, &state, &existed);
	check(error == 0, "create empty state for lock limit");
	for (index = 0; index < LOCKLINT_MAX_TRACKED_LOCKS; index++) {
		error = context_state_set_lock(&function, state,
		    (const struct lock_identity *)&identities[index], 1,
		    &next, &existed);
		check(error == 0 && !existed, "add lock below state limit");
		state = next;
	}
	check(context_state_lock_count(state) == LOCKLINT_MAX_TRACKED_LOCKS,
	    "state reaches tracked-lock limit");
	next = NULL;
	existed = true;
	error = context_state_set_lock(&function, state,
	    (const struct lock_identity *)&identities[
	    LOCKLINT_MAX_TRACKED_LOCKS], 1, &next, &existed);
	check(error == E2BIG, "lock beyond state limit is rejected");
	check(next == NULL && existed, "limit error preserves output arguments");

	context_collection_free(&function);
}

static void
test_visibility_state_interning(void)
{
	struct function_info function = { 0 };
	unsigned int identities[2];
	const struct lock_identity *first =
	    (const struct lock_identity *)&identities[0];
	const struct lock_identity *second =
	    (const struct lock_identity *)&identities[1];
	struct semantic_state *empty;
	struct semantic_state *first_invisible;
	struct semantic_state *first_second;
	struct semantic_state *second_visible;
	struct semantic_state *changed;
	struct semantic_state *same;
	struct semantic_state *locked;
	struct semantic_state *whole_invisible;
	struct semantic_state *member_visible;
	struct semantic_state *whole_visible;
	enum semantic_visibility visibility;
	bool existed;
	int error;

	context_collection_create(&function);
	error = context_empty_state_intern(&function, &empty, &existed);
	check(error == 0, "create empty state for visibility");
	check(context_state_visibility_count(empty) == 0,
	    "empty state has no visibility entries");

	error = context_state_set_visibility(&function, empty,
	    REGION(first, 0, 4),
	    SEMANTIC_VISIBILITY_INVISIBLE, &first_invisible, &existed);
	check(error == 0 && !existed, "create one-entry visibility state");
	check(context_state_visibility(first_invisible, REGION(first, 0, 4),
	    &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_INVISIBLE,
	    "visibility state records invisible object");
	check(!context_state_visibility(empty, REGION(first, 0, 4), &visibility),
	    "visibility transition preserves empty state");

	error = context_state_set_visibility(&function, first_invisible,
	    REGION(first, 0, 4),
	    SEMANTIC_VISIBILITY_INVISIBLE, &same, &existed);
	check(error == 0 && existed && same == first_invisible,
	    "unchanged visibility state is reused");

	error = context_state_set_visibility(&function, first_invisible,
	    REGION(second, 0, 4),
	    SEMANTIC_VISIBILITY_VISIBLE, &first_second, &existed);
	check(error == 0 && !existed, "create two-entry visibility state");
	error = context_state_set_visibility(&function, empty,
	    REGION(second, 0, 4),
	    SEMANTIC_VISIBILITY_VISIBLE, &second_visible, &existed);
	check(error == 0 && !existed, "create alternate visibility state");
	error = context_state_set_visibility(&function, second_visible,
	    REGION(first, 0, 4),
	    SEMANTIC_VISIBILITY_INVISIBLE, &same, &existed);
	check(error == 0 && existed && same == first_second,
	    "visibility insertion order reuses canonical state");

	error = context_state_set_visibility(&function, first_second,
	    REGION(first, 0, 4),
	    SEMANTIC_VISIBILITY_VISIBLE, &changed, &existed);
	check(error == 0 && !existed, "create changed visibility state");
	check(context_state_visibility(changed, REGION(first, 0, 4),
	    &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "explicit visible state replaces invisible state");
	check(context_state_visibility(first_second, REGION(first, 0, 4),
	    &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_INVISIBLE,
	    "visibility replacement preserves prior state");
	error = context_state_set_visibility(&function, empty,
	    REGION(first, 0, 16), SEMANTIC_VISIBILITY_INVISIBLE,
	    &whole_invisible, &existed);
	check(error == 0 && !existed, "create invisible containing region");
	error = context_state_set_visibility(&function, whole_invisible,
	    REGION(first, 0, 4), SEMANTIC_VISIBILITY_VISIBLE,
	    &member_visible, &existed);
	check(error == 0 && !existed &&
	    context_state_visibility_count(member_visible) == 2,
	    "narrower visible region retains containing rule");
	visibility = SEMANTIC_VISIBILITY_INVISIBLE;
	check(context_state_effective_visibility(member_visible,
	    REGION(first, 0, 4), &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "exact member visibility overrides whole object");
	check(context_state_effective_visibility(member_visible,
	    REGION(first, 1, 2), &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "member visibility covers descendants");
	check(context_state_effective_visibility(member_visible,
	    REGION(first, 8, 4), &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_INVISIBLE,
	    "whole-object visibility covers sibling members");
	visibility = SEMANTIC_VISIBILITY_INVISIBLE;
	check(!context_state_effective_visibility(member_visible,
	    REGION(second, 0, 4), &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "unmentioned object defaults to visible");
	error = context_state_set_visibility(&function, member_visible,
	    REGION(first, 0, 16), SEMANTIC_VISIBILITY_VISIBLE,
	    &whole_visible, &existed);
	check(error == 0 && !existed &&
	    context_state_visibility_count(whole_visible) == 1 &&
	    !context_state_visibility(whole_visible, REGION(first, 0, 4),
	    &visibility),
	    "new containing region removes subsumed override");
	check(context_state_effective_visibility(whole_visible,
	    REGION(first, 0, 4), &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "later broad visibility replaces member override");
	visibility = SEMANTIC_VISIBILITY_INVISIBLE;
	check(!context_state_effective_visibility(whole_visible,
	    REGION(first, INT64_MAX, 2), &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "overflowing visibility query defaults to visible");

	error = context_state_set_lock(&function, first_second, first, 1,
	    &locked, &existed);
	check(error == 0 && locked->visibility == first_second->visibility,
	    "lock transition shares visibility set");
	check(context_visibility_set_count(&function) == 8,
	    "function owns eight canonical visibility sets");
	check(context_visibility_sets_created(&function) == 8,
	    "visibility set creation count is retained");
	check(context_visibility_sets_reused(&function) != 0,
	    "visibility set reuse count is retained");

	context_collection_free(&function);
}

static void
test_visibility_state_limit(void)
{
	struct function_info function = { 0 };
	unsigned int identities[LOCKLINT_MAX_TRACKED_VISIBILITY + 1];
	struct semantic_state *state;
	struct semantic_state *next;
	bool existed;
	size_t index;
	int error;

	context_collection_create(&function);
	error = context_empty_state_intern(&function, &state, &existed);
	check(error == 0, "create empty state for visibility limit");
	for (index = 0; index < LOCKLINT_MAX_TRACKED_VISIBILITY; index++) {
		error = context_state_set_visibility(&function, state,
		    REGION(&identities[index], 0, 4),
		    SEMANTIC_VISIBILITY_INVISIBLE, &next, &existed);
		check(error == 0 && !existed,
		    "add visibility entry below state limit");
		state = next;
	}
	check(context_state_visibility_count(state) ==
	    LOCKLINT_MAX_TRACKED_VISIBILITY,
	    "state reaches tracked-visibility limit");
	next = NULL;
	existed = true;
	error = context_state_set_visibility(&function, state,
	    REGION(&identities[LOCKLINT_MAX_TRACKED_VISIBILITY], 0, 4),
	    SEMANTIC_VISIBILITY_INVISIBLE, &next, &existed);
	check(error == E2BIG, "visibility beyond state limit is rejected");
	check(next == NULL && existed,
	    "visibility limit error preserves output arguments");

	context_collection_free(&function);
}

static void
test_function_ownership(void)
{
	struct function_info first = { 0 };
	struct function_info second = { 0 };
	unsigned int identity;
	const struct lock_identity *lock =
	    (const struct lock_identity *)&identity;
	struct semantic_state *first_state;
	struct semantic_state *first_held;
	struct semantic_state *second_state;
	struct semantic_state *second_held;
	struct semantic_state *first_visible;
	struct semantic_state *second_visible;
	struct semantic_state *same;
	struct function_context *first_context;
	struct function_context *second_context;
	enum semantic_visibility visibility;
	bool existed;
	int error;

	context_collection_create(&first);
	context_collection_create(&second);
	error = context_empty_state_intern(&first, &first_state, &existed);
	check(error == 0, "create first function state");
	error = context_state_set_lock(&first, first_state, lock, 1,
	    &first_held, &existed);
	check(error == 0 && !existed, "create first function held state");

	error = context_empty_state_intern(&second, &second_state, &existed);
	check(error == 0, "create second function state");
	check(!existed, "second function owns a distinct semantic state");
	check(second_state != first_state,
	    "semantic states are interned per function");
	error = context_state_import(&second, first_held, &second_held,
	    &existed);
	check(error == 0 && !existed, "import held state into second function");
	check(second_held != first_held,
	    "imported state is owned by destination function");
	check(context_state_lock_modes(second_held, lock) == 1,
	    "imported state preserves held lock");
	error = context_state_import(&second, first_held, &same, &existed);
	check(error == 0 && existed && same == second_held,
	    "repeated state import reuses destination state");
	error = context_state_set_visibility(&first, first_held,
	    REGION(lock, 0, 4),
	    SEMANTIC_VISIBILITY_VISIBLE, &first_visible, &existed);
	check(error == 0, "create first function visibility state");
	error = context_state_import(&second, first_visible, &second_visible,
	    &existed);
	check(error == 0 && !existed && second_visible != first_visible &&
	    context_state_visibility(second_visible, REGION(lock, 0, 4),
	    &visibility) &&
	    visibility == SEMANTIC_VISIBILITY_VISIBLE,
	    "state import preserves visibility in destination state");

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

static void
test_point_state_competition_widening(void)
{
	struct function_info function = { 0 };
	struct semantic_state *zero;
	struct semantic_state *one;
	struct semantic_state *two;
	struct semantic_state *minus_one;
	struct semantic_state *held;
	struct semantic_state *visible;
	struct function_context *context;
	struct point_state *point_state;
	struct point_state *same;
	struct competition_interval competition;
	unsigned int identity;
	char blocks[2];
	char instruction;
	struct analysis_point upward = {
		.block = (struct basic_block *)&blocks[0],
		.next_instruction = (struct instruction *)&instruction
	};
	struct analysis_point downward = {
		.block = (struct basic_block *)&blocks[1],
		.next_instruction = (struct instruction *)&instruction
	};
	bool existed;
	bool widened;
	int error;

	context_collection_create(&function);
	error = context_empty_state_intern(&function, &zero, &existed);
	check(error == 0, "create widening zero state");
	error = context_state_set_competition(&function, zero,
	    (struct competition_interval){ .minimum = 1, .maximum = 1 },
	    &one, &existed);
	check(error == 0, "create widening one state");
	error = context_state_set_competition(&function, zero,
	    (struct competition_interval){ .minimum = 2, .maximum = 2 },
	    &two, &existed);
	check(error == 0, "create widening two state");
	error = context_state_set_competition(&function, zero,
	    (struct competition_interval){ .minimum = -1, .maximum = -1 },
	    &minus_one, &existed);
	check(error == 0, "create widening negative state");
	error = context_state_set_lock(&function, zero,
	    (const struct lock_identity *)&identity, 1, &held, &existed);
	check(error == 0, "create widening different-lock state");
	error = context_state_set_visibility(&function, zero,
	    REGION(&identity, 0, 4),
	    SEMANTIC_VISIBILITY_INVISIBLE, &visible, &existed);
	check(error == 0, "create widening different-visibility state");
	error = context_create(&function, NULL, zero, &context, &existed);
	check(error == 0, "create widening context");

	error = context_point_state_record(context, upward, zero, &point_state,
	    &existed);
	check(error == 0 && !existed, "record upward loop entry");
	error = context_point_state_record_widened(context, upward, one,
	    &point_state, &existed, &widened);
	competition = context_state_competition(point_state->state);
	check(error == 0 && !existed && widened && competition.minimum == 0 &&
	    competition.maximum_unbounded,
	    "upward back edge widens upper endpoint");
	error = context_point_state_record_widened(context, upward, two,
	    &same, &existed, &widened);
	check(error == 0 && existed && !widened && same == point_state,
	    "upper-unbounded point covers later arrival");
	error = context_point_state_record_widened(context, upward, held,
	    &same, &existed, &widened);
	check(error == 0 && !existed && !widened && same->state == held,
	    "different lock set remains separate at widened point");
	error = context_point_state_record_widened(context, upward, visible,
	    &same, &existed, &widened);
	check(error == 0 && !existed && !widened && same->state == visible,
	    "different visibility set remains separate at widened point");

	error = context_point_state_record(context, downward, zero,
	    &point_state, &existed);
	check(error == 0 && !existed, "record downward loop entry");
	error = context_point_state_record_widened(context, downward, minus_one,
	    &point_state, &existed, &widened);
	competition = context_state_competition(point_state->state);
	check(error == 0 && !existed && widened &&
	    competition.minimum_unbounded &&
	    competition.maximum == 0,
	    "downward back edge widens lower endpoint");

	context_collection_free(&function);
}

int
main(void)
{
	test_exit_state_mapping();
	test_state_interning();
	test_context_interning();
	test_competition_depth_interning();
	test_lock_state_interning();
	test_lock_state_limit();
	test_visibility_state_interning();
	test_visibility_state_limit();
	test_function_ownership();
	test_point_state_interning();
	test_point_state_competition_widening();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
