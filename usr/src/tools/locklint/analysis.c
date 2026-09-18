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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "analysis.h"
#include "callgraph.h"
#include "context.h"
#include "dependency.h"
#include "function_info.h"
#include "identity.h"
#include "lib.h"
#include "linearize.h"
#include "provenance.h"
#include "symbol.h"
#include "worklist.h"

#define	DISTRIBUTION_EXACT_MAX	5

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

/*
 * Exact small-count buckets expose the boundary used to choose between lists
 * and indexed collections.  Two fixed overflow buckets retain useful shape
 * without memory proportional to the largest observed owner.
 */
struct distribution {
	size_t samples;
	size_t total;
	size_t maximum;
	size_t exact[DISTRIBUTION_EXACT_MAX + 1];
	size_t six_to_eight;
	size_t nine_or_more;
	const struct function_info *maximum_owner;
};

struct analysis_measurements {
	struct distribution contexts_per_function;
	struct distribution semantic_states_per_function;
	struct distribution point_states_per_context;
	struct distribution states_per_analysis_point;
	struct distribution exits_per_context;
	struct distribution continuations_per_context;
	struct distribution provenance_edges_per_context;
	struct distribution locks_per_semantic_state;
	struct distribution visibility_per_semantic_state;
	size_t semantic_state_bytes;
	size_t context_bytes;
	size_t point_state_bytes;
	size_t exit_bytes;
	size_t continuation_bytes;
	size_t provenance_edge_bytes;
};

struct analysis {
	struct worklist worklist;
	struct analysis_counts counts;
	struct analysis_measurements measurements;
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

		for (continuation = dependency_continuation_first(
		    point_state->context); continuation != NULL;
		    continuation = dependency_continuation_next(
		    point_state->context, continuation))
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
distribution_add(struct distribution *distribution, size_t count,
    const struct function_info *owner)
{
	bool first = distribution->samples == 0;

	if (distribution->samples == SIZE_MAX ||
	    count > SIZE_MAX - distribution->total)
		die("analysis distribution counter overflow");
	distribution->samples++;
	distribution->total += count;
	if (first || count > distribution->maximum) {
		distribution->maximum = count;
		distribution->maximum_owner = owner;
	}
	if (count <= DISTRIBUTION_EXACT_MAX)
		distribution->exact[count]++;
	else if (count <= 8)
		distribution->six_to_eight++;
	else
		distribution->nine_or_more++;
}

static void
distribution_add_zeroes(struct distribution *distribution, size_t count)
{
	if (count > SIZE_MAX - distribution->samples ||
	    count > SIZE_MAX - distribution->exact[0])
		die("analysis distribution counter overflow");
	distribution->samples += count;
	distribution->exact[0] += count;
}

static void
memory_add(size_t *total, size_t count, size_t size)
{
	if (count > (SIZE_MAX - *total) / size)
		die("retained collection memory estimate overflow");
	*total += count * size;
}

static bool
same_analysis_point(const struct point_state *left,
    const struct point_state *right)
{
	return (left->point.block == right->point.block &&
	    left->point.next_instruction == right->point.next_instruction);
}

static void
measure_point_states(struct analysis_measurements *measurements,
    struct function_context *context)
{
	struct point_state *point_state;
	struct point_state *previous = NULL;
	size_t states_at_point = 0;
	size_t point_states = context_point_state_count(context);

	distribution_add(&measurements->point_states_per_context, point_states,
	    context->function);
	memory_add(&measurements->point_state_bytes, point_states,
	    sizeof (struct point_state));
	for (point_state = avl_first(&context->point_states);
	    point_state != NULL;
	    point_state = AVL_NEXT(&context->point_states, point_state)) {
		if (previous != NULL &&
		    !same_analysis_point(previous, point_state)) {
			distribution_add(
			    &measurements->states_per_analysis_point,
			    states_at_point, context->function);
			states_at_point = 0;
		}
		states_at_point++;
		previous = point_state;
	}
	if (states_at_point != 0) {
		distribution_add(&measurements->states_per_analysis_point,
		    states_at_point, context->function);
	}
}

static void
measure_context(struct analysis_measurements *measurements,
    struct function_context *context)
{
	size_t count;

	measure_point_states(measurements, context);

	count = dependency_exit_count(context);
	distribution_add(&measurements->exits_per_context, count,
	    context->function);
	memory_add(&measurements->exit_bytes, count,
	    sizeof (struct context_exit));

	count = dependency_continuation_count(context);
	distribution_add(&measurements->continuations_per_context, count,
	    context->function);
	memory_add(&measurements->continuation_bytes, count,
	    sizeof (struct continuation));

	count = provenance_edge_count(context);
	distribution_add(&measurements->provenance_edges_per_context, count,
	    context->function);
	memory_add(&measurements->provenance_edge_bytes, count,
	    sizeof (struct provenance_edge));
}

/*
 * Measure retained records in one pass after the fixed point.  AVL order
 * groups point states by analysis point, so no temporary point index or
 * per-owner sample array is needed.
 */
static void
measure_collections(struct analysis *analysis)
{
	struct analysis_measurements *measurements = &analysis->measurements;
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct function_context *context;
		size_t contexts = context_count(function);
		size_t states = context_state_count(function);

		distribution_add(&measurements->contexts_per_function, contexts,
		    function);
		distribution_add(&measurements->semantic_states_per_function,
		    states, function);
		distribution_add_zeroes(
		    &measurements->locks_per_semantic_state, states);
		distribution_add_zeroes(
		    &measurements->visibility_per_semantic_state, states);
		memory_add(&measurements->context_bytes, contexts,
		    sizeof (struct function_context));
		memory_add(&measurements->semantic_state_bytes, states,
		    sizeof (struct semantic_state));

		for (context = avl_first(&function->contexts.contexts);
		    context != NULL;
		    context = AVL_NEXT(&function->contexts.contexts, context))
			measure_context(measurements, context);
	}
	callgraph_iter_close(iterator);
}

static void
show_distribution(FILE *stream, const char *name,
    const struct distribution *distribution)
{
	(void) fprintf(stream,
	    "distribution %s samples %zu total %zu max %zu bins "
	    "0:%zu 1:%zu 2:%zu 3:%zu 4:%zu 5:%zu 6-8:%zu 9+:%zu\n",
	    name, distribution->samples, distribution->total,
	    distribution->maximum, distribution->exact[0],
	    distribution->exact[1], distribution->exact[2],
	    distribution->exact[3], distribution->exact[4],
	    distribution->exact[5], distribution->six_to_eight,
	    distribution->nine_or_more);
}

static const char *
function_name(const struct function_info *function)
{
	struct ident *ident = function->ep->name->ident;

	return (ident != NULL ? show_ident(ident) : "<anonymous>");
}

static void
show_maximum_owner(FILE *stream, const char *name,
    const struct distribution *distribution)
{
	const struct function_info *owner = distribution->maximum_owner;

	if (owner == NULL)
		return;
	(void) fprintf(stream, "maximum %s %zu function %s tu=%s\n",
	    name, distribution->maximum, function_name(owner),
	    locklint_translation_unit_file(owner->tu));
}

static size_t
retained_collection_bytes(const struct analysis_measurements *measurements)
{
	size_t total = measurements->semantic_state_bytes;
	size_t values[] = {
		measurements->context_bytes,
		measurements->point_state_bytes,
		measurements->exit_bytes,
		measurements->continuation_bytes,
		measurements->provenance_edge_bytes
	};
	size_t index;

	for (index = 0; index < sizeof (values) / sizeof (values[0]); index++) {
		if (values[index] > SIZE_MAX - total)
			die("retained collection memory estimate overflow");
		total += values[index];
	}
	return (total);
}

static void
show_counts(FILE *stream, const struct analysis *analysis)
{
	const struct analysis_counts *counts = &analysis->counts;
	const struct analysis_measurements *measurements =
	    &analysis->measurements;

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
	show_distribution(stream, "contexts/function",
	    &measurements->contexts_per_function);
	show_distribution(stream, "semantic-states/function",
	    &measurements->semantic_states_per_function);
	show_distribution(stream, "point-states/context",
	    &measurements->point_states_per_context);
	show_distribution(stream, "states/analysis-point",
	    &measurements->states_per_analysis_point);
	show_distribution(stream, "exits/context",
	    &measurements->exits_per_context);
	show_distribution(stream, "continuations/context",
	    &measurements->continuations_per_context);
	show_distribution(stream, "provenance-edges/context",
	    &measurements->provenance_edges_per_context);
	show_distribution(stream, "locks/semantic-state",
	    &measurements->locks_per_semantic_state);
	show_distribution(stream, "visibility/semantic-state",
	    &measurements->visibility_per_semantic_state);
	show_maximum_owner(stream, "contexts/function",
	    &measurements->contexts_per_function);
	show_maximum_owner(stream, "semantic-states/function",
	    &measurements->semantic_states_per_function);
	show_maximum_owner(stream, "point-states/context",
	    &measurements->point_states_per_context);
	show_maximum_owner(stream, "states/analysis-point",
	    &measurements->states_per_analysis_point);
	show_maximum_owner(stream, "exits/context",
	    &measurements->exits_per_context);
	show_maximum_owner(stream, "continuations/context",
	    &measurements->continuations_per_context);
	show_maximum_owner(stream, "provenance-edges/context",
	    &measurements->provenance_edges_per_context);
	(void) fprintf(stream, "memory semantic-states %zu bytes\n",
	    measurements->semantic_state_bytes);
	(void) fprintf(stream, "memory contexts %zu bytes\n",
	    measurements->context_bytes);
	(void) fprintf(stream, "memory point-states %zu bytes\n",
	    measurements->point_state_bytes);
	(void) fprintf(stream, "memory exits %zu bytes\n",
	    measurements->exit_bytes);
	(void) fprintf(stream, "memory continuations %zu bytes\n",
	    measurements->continuation_bytes);
	(void) fprintf(stream, "memory provenance-edges %zu bytes\n",
	    measurements->provenance_edge_bytes);
	(void) fprintf(stream, "memory retained-collections %zu bytes\n",
	    retained_collection_bytes(measurements));
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
	measure_collections(&analysis);
	show_counts(stream, &analysis);
}
