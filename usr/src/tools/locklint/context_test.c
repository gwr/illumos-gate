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
	struct semantic_state *mapped;
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
	    callee_exit, NULL, NULL, &mapped, &existed);
	check(error == 0 && existed && mapped == caller_empty,
	    "exit mapping removes released inherited lock");
	check(context_state_lock_modes(mapped, second) == 0,
	    "exit mapping filters unapproved new lock");
	error = context_state_map_exit(&caller, caller_held, callee_entry,
	    callee_exit, include_selected_lock, (void *)second, &mapped,
	    &existed);
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
	    callee_deep, include_selected_lock, (void *)second, &mapped,
	    &existed);
	check(error == 0 && !existed &&
	    context_state_competition(mapped).minimum == 2 &&
	    context_state_competition(mapped).maximum == 2,
	    "exit mapping preserves competition depth");

	context_collection_free(&callee);
	context_collection_free(&caller);
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
	struct semantic_state *same;
	struct function_context *first_context;
	struct function_context *second_context;
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
	test_exit_state_mapping();
	test_state_interning();
	test_context_interning();
	test_competition_depth_interning();
	test_lock_state_interning();
	test_lock_state_limit();
	test_function_ownership();
	test_point_state_interning();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
