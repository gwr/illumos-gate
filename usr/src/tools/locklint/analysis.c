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
 * Drive the caller-context fixed point over retained Sparse control-flow
 * graphs.  The initial implementation carries only the canonical empty state.
 * Resolved calls suspend their caller until a callee exit reactivates the
 * corresponding continuation; unresolved calls have no semantic effect.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "analysis.h"
#include "callgraph.h"
#include "context.h"
#include "dependency.h"
#include "function_info.h"
#include "lib.h"
#include "linearize.h"
#include "provenance.h"
#include "worklist.h"

struct analysis_counts {
	size_t roots;
	size_t functions;
	size_t semantic_states_created;
	size_t semantic_states_reused;
	size_t contexts_created;
	size_t contexts_reused;
	size_t point_states_created;
	size_t point_states_reused;
	size_t exits_created;
	size_t exits_reused;
	size_t continuations_created;
	size_t continuations_reused;
	size_t provenance_edges_created;
	size_t provenance_edges_reused;
	size_t reactivations;
};

struct analysis {
	struct worklist worklist;
	struct analysis_counts counts;
};

static struct instruction *
first_live_instruction(struct basic_block *block)
{
	struct instruction *instruction;

	FOR_EACH_PTR(block->insns, instruction) {
		if (instruction->bb != NULL)
			return (instruction);
	} END_FOR_EACH_PTR(instruction);
	return (NULL);
}

static struct instruction *
next_live_instruction(struct basic_block *block, struct instruction *current)
{
	struct instruction *instruction;
	bool found = false;

	FOR_EACH_PTR(block->insns, instruction) {
		if (instruction->bb == NULL)
			continue;
		if (found)
			return (instruction);
		if (instruction == current)
			found = true;
	} END_FOR_EACH_PTR(instruction);
	if (!found)
		die("analysis point instruction is not in its basic block");
	return (NULL);
}

/*
 * Record one reachable point and schedule it only when newly discovered.
 * Existing point states terminate CFG cycles without another queue search.
 */
static void
record_point(struct analysis *analysis, struct function_context *context,
    struct basic_block *block, struct instruction *instruction,
    const struct semantic_state *state)
{
	struct analysis_point point = {
		.block = block,
		.next_instruction = instruction
	};
	struct point_state *point_state;
	bool existed;
	int error;

	error = context_point_state_record(context, point, state, &point_state,
	    &existed);
	if (error != 0)
		die("cannot record analysis point: %s", strerror(error));
	if (existed) {
		analysis->counts.point_states_reused++;
		return;
	}
	analysis->counts.point_states_created++;
	if (!worklist_point_state_enqueue(&analysis->worklist, point_state))
		die("new analysis point was already queued");
}

static void
record_reactivation(struct analysis *analysis,
    struct continuation *continuation)
{
	const struct context_exit *exit;

	while ((exit =
	    dependency_continuation_next_exit(continuation)) != NULL) {
		struct point_state *point_state;
		bool existed;
		int error;

		error = dependency_continuation_apply_exit(continuation, exit,
		    continuation->caller_state, &analysis->worklist,
		    &point_state, &existed);
		if (error != 0)
			die("cannot apply context exit: %s", strerror(error));
		analysis->counts.reactivations++;
		if (existed)
			analysis->counts.point_states_reused++;
		else
			analysis->counts.point_states_created++;
	}
}

/*
 * Publish a newly observed exit, then make it available to every waiting
 * caller.  Duplicate publication adds no generation and therefore no work.
 */
static void
publish_exit(struct analysis *analysis, struct point_state *point_state)
{
	struct context_exit *exit;
	bool existed;
	int error;

	error = dependency_exit_publish(point_state->context, point_state->state,
	    &exit, &existed);
	if (error != 0)
		die("cannot publish context exit: %s", strerror(error));
	if (existed) {
		analysis->counts.exits_reused++;
		return;
	}
	analysis->counts.exits_created++;

	{
		struct continuation *continuation;

		SLIST_FOREACH(continuation,
		    &point_state->context->continuations, link)
			record_reactivation(analysis, continuation);
	}
}

static void
process_call(struct analysis *analysis, struct point_state *point_state)
{
	struct function_context *caller_context = point_state->context;
	struct function_info *callee_function;
	struct function_context *callee_context;
	struct semantic_state *callee_state;
	struct continuation *continuation;
	struct provenance_edge *edge;
	struct analysis_point resume_point;
	bool context_existed;
	bool existed;
	int error;

	callee_function = callgraph_callee(caller_context->function,
	    point_state->point.next_instruction);
	if (callee_function == NULL) {
		record_point(analysis, caller_context, point_state->point.block,
		    next_live_instruction(point_state->point.block,
		    point_state->point.next_instruction), point_state->state);
		return;
	}

	error = context_empty_state_intern(callee_function, &callee_state,
	    &existed);
	if (error != 0)
		die("cannot intern callee state: %s", strerror(error));
	if (existed)
		analysis->counts.semantic_states_reused++;
	else
		analysis->counts.semantic_states_created++;

	error = context_create(callee_function, NULL, callee_state,
	    &callee_context, &context_existed);
	if (error != 0)
		die("cannot create callee context: %s", strerror(error));
	if (context_existed) {
		analysis->counts.contexts_reused++;
	} else {
		analysis->counts.contexts_created++;
		analysis->counts.functions++;
	}

	error = provenance_edge_create(callee_context, caller_context,
	    point_state->point.next_instruction, &edge, &existed);
	if (error != 0)
		die("cannot record context provenance: %s", strerror(error));
	if (existed)
		analysis->counts.provenance_edges_reused++;
	else
		analysis->counts.provenance_edges_created++;

	resume_point.block = point_state->point.block;
	resume_point.next_instruction = next_live_instruction(
	    point_state->point.block, point_state->point.next_instruction);
	error = dependency_continuation_create(callee_context, caller_context,
	    resume_point, point_state->state, NULL, &continuation, &existed);
	if (error != 0)
		die("cannot create call continuation: %s", strerror(error));
	if (existed)
		analysis->counts.continuations_reused++;
	else
		analysis->counts.continuations_created++;

	if (!context_existed) {
		record_point(analysis, callee_context,
		    callee_function->ep->entry->bb,
		    first_live_instruction(callee_function->ep->entry->bb),
		    callee_state);
	}
	record_reactivation(analysis, continuation);
}

/*
 * Advance one point by one instruction, or fan a block-exit state out to its
 * CFG successors.  Keeping these transitions as separate queued facts makes
 * state reuse at joins and loop headers directly measurable.
 */
static void
process_point(struct analysis *analysis, struct point_state *point_state)
{
	struct analysis_point point = point_state->point;

	if (point.next_instruction != NULL) {
		if (point.next_instruction->opcode == OP_RET) {
			publish_exit(analysis, point_state);
			return;
		}
		if (point.next_instruction->opcode == OP_CALL) {
			process_call(analysis, point_state);
			return;
		}
		record_point(analysis, point_state->context, point.block,
		    next_live_instruction(point.block, point.next_instruction),
		    point_state->state);
		return;
	}

	{
		struct basic_block *child;

		FOR_EACH_PTR(point.block->children, child) {
			record_point(analysis, point_state->context, child,
			    first_live_instruction(child), point_state->state);
		} END_FOR_EACH_PTR(child);
	}
}

static void
seed_root(struct analysis *analysis, struct function_info *function)
{
	struct function_context *context;
	struct semantic_state *state;
	bool existed;
	int error;

	analysis->counts.roots++;
	error = context_empty_state_intern(function, &state, &existed);
	if (error != 0)
		die("cannot intern root state: %s", strerror(error));
	if (existed)
		analysis->counts.semantic_states_reused++;
	else
		analysis->counts.semantic_states_created++;

	error = context_create(function, NULL, state, &context, &existed);
	if (error != 0)
		die("cannot create root context: %s", strerror(error));
	if (existed) {
		analysis->counts.contexts_reused++;
	} else {
		analysis->counts.contexts_created++;
		analysis->counts.functions++;
	}
	record_point(analysis, context, function->ep->entry->bb,
	    first_live_instruction(function->ep->entry->bb), state);
}

static void
seed_roots(struct analysis *analysis)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		if (function->root_reasons != 0)
			seed_root(analysis, function);
	}
	callgraph_iter_close(iterator);
}

static void
show_counts(FILE *stream, const struct analysis *analysis)
{
	const struct analysis_counts *counts = &analysis->counts;

	(void) fprintf(stream, "roots %zu\n", counts->roots);
	(void) fprintf(stream, "functions %zu\n", counts->functions);
	(void) fprintf(stream, "semantic-states created %zu reused %zu\n",
	    counts->semantic_states_created, counts->semantic_states_reused);
	(void) fprintf(stream, "contexts created %zu reused %zu\n",
	    counts->contexts_created, counts->contexts_reused);
	(void) fprintf(stream, "point-states created %zu reused %zu\n",
	    counts->point_states_created, counts->point_states_reused);
	(void) fprintf(stream, "exits created %zu reused %zu\n",
	    counts->exits_created, counts->exits_reused);
	(void) fprintf(stream, "continuations created %zu reused %zu\n",
	    counts->continuations_created, counts->continuations_reused);
	(void) fprintf(stream, "provenance-edges created %zu reused %zu\n",
	    counts->provenance_edges_created,
	    counts->provenance_edges_reused);
	(void) fprintf(stream, "reactivations %zu\n", counts->reactivations);
	(void) fprintf(stream, "worklist peak %zu\n",
	    analysis->worklist.peak_length);
}

void
analysis_run(FILE *stream)
{
	struct analysis analysis = { 0 };
	struct point_state *point_state;

	worklist_create(&analysis.worklist);
	seed_roots(&analysis);
	while ((point_state =
	    worklist_point_state_dequeue(&analysis.worklist)) != NULL)
		process_point(&analysis, point_state);
	show_counts(stream, &analysis);
}
