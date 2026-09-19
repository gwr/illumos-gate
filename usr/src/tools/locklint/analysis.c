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
 * graphs.  Lock operations update immutable local semantic states, while
 * result-sensitive operations retain their selected outcome in the analysis
 * point until its branch.  Resolved calls distinguish canonical
 * pointer-formal bindings and transfer state through callees.  Diagnostics
 * consume the complete fixed point so branch-dependent lock state is reported
 * coherently.
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "access.h"
#include "analysis.h"
#include "annotations.h"
#include "binding.h"
#include "callgraph.h"
#include "context.h"
#include "dependency.h"
#include "diagnostics.h"
#include "events.h"
#include "expression.h"
#include "flowgraph.h"
#include "function_info.h"
#include "identity.h"
#include "lib.h"
#include "linearize.h"
#include "lock_identity.h"
#include "provenance.h"
#include "symbol.h"
#include "worklist.h"

#define	DISTRIBUTION_EXACT_MAX	5

struct analysis_counts {
	size_t roots;
	size_t functions;
	size_t semantic_states_created;
	size_t semantic_states_reused;
	size_t binding_environments_created;
	size_t binding_environments_reused;
	size_t binding_identities_composed;
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
	size_t lock_identities_created;
	size_t lock_identities_reused;
	size_t lock_identities_unresolved;
	size_t lock_transitions_applied;
	size_t lock_transitions_deferred;
	size_t visibility_transitions_applied;
	size_t visibility_transitions_deferred;
	size_t visibility_transitions_unresolved;
	size_t competition_transitions_applied;
	size_t competition_backedges_widened;
	size_t competition_backedges_covered;
	size_t return_states_mapped;
	size_t return_locks_filtered;
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
	struct distribution binding_environments_per_function;
	struct distribution bindings_per_environment;
	struct distribution semantic_states_per_function;
	struct distribution visibility_sets_per_function;
	struct distribution visibility_entries_per_set;
	struct distribution point_states_per_context;
	struct distribution states_per_analysis_point;
	struct distribution exits_per_context;
	struct distribution continuations_per_context;
	struct distribution provenance_edges_per_context;
	struct distribution locks_per_semantic_state;
	struct distribution visibility_per_semantic_state;
	size_t lock_identities;
	size_t lock_identity_analysis_objects;
	size_t lock_identity_types[LOCK_ANALYSIS_OBJECT_PSEUDO + 1];
	size_t lock_identity_bytes;
	size_t lock_set_bytes;
	size_t visibility_set_bytes;
	size_t visibility_sets_created;
	size_t visibility_sets_reused;
	size_t semantic_state_bytes;
	size_t binding_environment_bytes;
	size_t context_bytes;
	size_t point_state_bytes;
	size_t exit_bytes;
	size_t continuation_bytes;
	size_t provenance_edge_bytes;
};

struct analysis {
	struct lock_identity_collection *lock_identities;
	struct worklist worklist;
	struct analysis_counts counts;
	struct analysis_measurements measurements;
};

static int context_access_identity(struct analysis *,
    const struct function_context *, const struct locklint_access *,
    struct lock_identity **, bool *, bool *);
static int context_access_key(const struct function_context *,
    const struct locklint_access *, struct lock_identity_key *,
    enum lock_analysis_object_type *, bool *);
static int context_access_region(const struct function_context *,
    const struct locklint_access *, struct visibility_region *, bool *, bool *);
static bool identity_formal_argument(const struct function_info *,
    struct lock_identity_key, enum lock_analysis_object_type, unsigned int *);
static struct symbol *function_formal_argument(const struct function_info *,
    unsigned int);
static void collect_assumed_regions(void);
static void record_semantic_state(struct analysis *, bool);
static const char *function_name(const struct function_info *);

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
record_analysis_point(struct analysis *analysis,
    struct function_context *context, struct analysis_point point,
    const struct semantic_state *state, bool back_edge)
{
	struct point_state *point_state;
	bool existed;
	int error;

	if (back_edge) {
		bool widened;

		error = context_point_state_record_widened(context, point, state,
		    &point_state, &existed, &widened);
		if (error == 0) {
			if (widened)
				analysis->counts.competition_backedges_widened++;
			else if (existed)
				analysis->counts.competition_backedges_covered++;
		}
	} else {
		error = context_point_state_record(context, point, state,
		    &point_state, &existed);
	}
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
record_point(struct analysis *analysis, struct function_context *context,
    struct basic_block *block, struct instruction *instruction,
    const struct semantic_state *state, bool back_edge)
{
	struct analysis_point point = {
		.block = block,
		.next_instruction = instruction
	};

	record_analysis_point(analysis, context, point, state, back_edge);
}

static void
record_semantic_state(struct analysis *analysis, bool existed)
{
	if (existed)
		analysis->counts.semantic_states_reused++;
	else
		analysis->counts.semantic_states_created++;
}

static const struct semantic_state *
apply_competition_event(struct analysis *analysis,
    struct point_state *point_state)
{
	enum locklint_execution_kind kind;
	struct semantic_state *state;
	bool existed;
	int adjustment;
	int error;

	kind = locklint_get_execution_annotation(
	    point_state->point.next_instruction);
	if (kind == LOCKLINT_EXECUTION_COMPETITION)
		adjustment = 1;
	else if (kind == LOCKLINT_EXECUTION_NO_COMPETITION)
		adjustment = -1;
	else
		return (point_state->state);
	error = context_state_adjust_competition(point_state->context->function,
	    point_state->state, adjustment, &state, &existed);
	if (error != 0)
		die("cannot apply competition transition: %s", strerror(error));
	record_semantic_state(analysis, existed);
	analysis->counts.competition_transitions_applied++;
	return (state);
}

struct visibility_transition {
	struct analysis *analysis;
	struct point_state *point_state;
	const struct semantic_state *state;
	enum semantic_visibility visibility;
};

/*
 * Translate one source operand through the active caller bindings and apply
 * it immediately.  Visibility sets are expected to contain zero to four
 * entries, so the context layer's sorted flat-array copy is cheaper than
 * building an auxiliary indexed collection for a marker's operands.
 */
static void
apply_visibility_target(const struct locklint_access *access,
    const struct expression *expr, void *data_arg)
{
	struct visibility_transition *data = data_arg;
	struct visibility_region region;
	struct semantic_state *state;
	bool available;
	bool composed;
	bool existed;
	int error;

	(void) expr;
	if (access == NULL) {
		data->analysis->counts.visibility_transitions_unresolved++;
		return;
	}
	error = context_access_region(data->point_state->context, access,
	    &region, &available, &composed);
	if (error != 0)
		die("cannot identify visibility target: %s", strerror(error));
	if (!available) {
		data->analysis->counts.visibility_transitions_unresolved++;
		return;
	}
	if (composed)
		data->analysis->counts.binding_identities_composed++;
	error = context_state_set_visibility(
	    data->point_state->context->function, data->state, region,
	    data->visibility, &state, &existed);
	if (error != 0)
		die("cannot apply visibility transition: %s", strerror(error));
	record_semantic_state(data->analysis, existed);
	if (state == data->state)
		data->analysis->counts.visibility_transitions_deferred++;
	else
		data->analysis->counts.visibility_transitions_applied++;
	data->state = state;
}

static const struct semantic_state *
apply_visibility_event(struct analysis *analysis,
    struct point_state *point_state)
{
	enum locklint_execution_kind kind;
	struct visibility_transition transition = {
		.analysis = analysis,
		.point_state = point_state,
		.state = apply_competition_event(analysis, point_state)
	};

	kind = locklint_get_execution_annotation(
	    point_state->point.next_instruction);
	if (kind == LOCKLINT_EXECUTION_INVISIBLE)
		transition.visibility = SEMANTIC_VISIBILITY_INVISIBLE;
	else if (kind == LOCKLINT_EXECUTION_VISIBLE)
		transition.visibility = SEMANTIC_VISIBILITY_VISIBLE;
	else
		return (transition.state);
	(void) locklint_for_each_visibility_target(
	    point_state->context->function->tu,
	    point_state->point.next_instruction, apply_visibility_target,
	    &transition);
	return (transition.state);
}

struct exit_mapping {
	const struct function_context *caller_context;
	const struct binding_environment *bindings;
	size_t filtered;
};

static bool
binding_contains_analysis_object(const struct binding_environment *bindings,
    const void *analysis_object)
{
	size_t index;

	for (index = 0; index < bindings->count; index++) {
		if (bindings->entries[index].actual_identity->
		    key.analysis_object == analysis_object)
			return (true);
	}
	return (false);
}

static bool
return_lock_visible(const struct lock_identity *lock, void *data)
{
	struct exit_mapping *mapping = data;

	if (lock->analysis_object_type ==
	    LOCK_ANALYSIS_OBJECT_OBJECT_IDENTITY)
		return (true);
	if (binding_contains_analysis_object(mapping->bindings,
	    lock->key.analysis_object))
		return (true);
	mapping->filtered++;
	return (false);
}

/*
 * Visibility effects are recorded on eagerly composed actual identities.
 * Normalize an unbound root formal's lowered argument pseudo back to the
 * source formal used by caller-side access regions.  Other actuals and
 * globals already use caller-visible coordinates.
 */
static bool
return_visibility_region(const struct visibility_region *source,
    struct visibility_region *result, void *data)
{
	struct exit_mapping *mapping = data;
	size_t index;

	*result = *source;
	for (index = 0; index < mapping->bindings->count; index++) {
		const struct lock_identity *actual =
		    mapping->bindings->entries[index].actual_identity;
		struct symbol *formal;
		unsigned int argument;

		if (actual->key.analysis_object != source->analysis_object)
			continue;
		if (identity_formal_argument(mapping->caller_context->function,
		    actual->key, actual->analysis_object_type, &argument) &&
		    binding_environment_lookup(
		    mapping->caller_context->bindings, argument) == NULL &&
		    (formal = function_formal_argument(
		    mapping->caller_context->function, argument)) != NULL)
			result->analysis_object = formal;
		return (true);
	}
	return (true);
}

static void
record_reactivation(struct analysis *analysis,
    struct continuation *continuation)
{
	const struct context_exit *exit;

	while ((exit =
	    dependency_continuation_next_exit(continuation)) != NULL) {
		bool same_translation_unit =
		    continuation->caller_context->function->tu ==
		    continuation->callee_context->function->tu;
		struct exit_mapping mapping = {
			.caller_context = continuation->caller_context,
			.bindings = continuation->callee_bindings
		};
		struct semantic_state *mapped_state;
		struct point_state *point_state;
		bool state_existed;
		bool existed;
		int error;

		error = context_state_map_exit(
		    continuation->caller_context->function,
		    continuation->caller_state,
		    continuation->callee_context->entry_state, exit->state,
		    return_lock_visible, &mapping,
		    same_translation_unit ? return_visibility_region : NULL,
		    &mapping, &mapped_state, &state_existed);
		if (error != 0)
			die("cannot map context exit: %s", strerror(error));
		record_semantic_state(analysis, state_existed);
		analysis->counts.return_states_mapped++;
		analysis->counts.return_locks_filtered += mapping.filtered;
		error = dependency_continuation_apply_exit(continuation, exit,
		    mapped_state, &analysis->worklist,
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

static const struct semantic_state *
apply_lock_event(struct analysis *analysis, struct point_state *point_state)
{
	struct locklint_access access;
	struct lock_identity *identity;
	struct semantic_state *state;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;
	bool composed;
	bool existed;
	int error;

	action = locklint_get_lock_action(point_state->context->function->tu,
	    point_state->point.next_instruction, &access, &mode);
	if (action == LOCKLINT_LOCK_NONE)
		return (point_state->state);
	if (access.root == NULL) {
		analysis->counts.lock_identities_unresolved++;
		return (point_state->state);
	}
	error = context_access_identity(analysis, point_state->context, &access,
	    &identity, &existed, &composed);
	if (error != 0)
		die("cannot identify lock event: %s", strerror(error));
	if (composed)
		analysis->counts.binding_identities_composed++;
	if (existed)
		analysis->counts.lock_identities_reused++;
	else
		analysis->counts.lock_identities_created++;
	if (action != LOCKLINT_LOCK_ACQUIRE &&
	    action != LOCKLINT_LOCK_RELEASE &&
	    action != LOCKLINT_LOCK_DOWNGRADE) {
		analysis->counts.lock_transitions_deferred++;
		return (point_state->state);
	}
	error = context_state_set_lock(point_state->context->function,
	    point_state->state, identity,
	    action == LOCKLINT_LOCK_ACQUIRE ? mode :
	    action == LOCKLINT_LOCK_DOWNGRADE ? LOCKLINT_MODE_READER : 0,
	    &state, &existed);
	if (error != 0)
		die("cannot apply lock event: %s", strerror(error));
	record_semantic_state(analysis, existed);
	analysis->counts.lock_transitions_applied++;
	return (state);
}

static struct symbol *
function_type(const struct function_info *function)
{
	struct symbol *type = function->ep->name->ctype.base_type;

	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type != NULL && type->type == SYM_FN ? type : NULL);
}

static bool
formal_is_pointer(const struct symbol *formal)
{
	const struct symbol *type = formal->ctype.base_type;

	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type != NULL && type->type == SYM_PTR);
}

static bool
identity_formal_argument(const struct function_info *function,
    struct lock_identity_key key,
    enum lock_analysis_object_type object_type, unsigned int *argument)
{
	struct symbol *formal;
	struct symbol *type;
	unsigned int current = 0;

	if (object_type == LOCK_ANALYSIS_OBJECT_PSEUDO) {
		const struct pseudo *pseudo = key.analysis_object;

		if (pseudo->type != PSEUDO_ARG || pseudo->nr == 0)
			return (false);
		*argument = pseudo->nr - 1;
		return (true);
	}
	if (object_type != LOCK_ANALYSIS_OBJECT_SYMBOL)
		return (false);
	type = function_type(function);
	if (type == NULL)
		return (false);
	FOR_EACH_PTR(type->arguments, formal) {
		if (formal == key.analysis_object) {
			*argument = current;
			return (true);
		}
		current++;
	} END_FOR_EACH_PTR(formal);
	return (false);
}

static struct symbol *
function_formal_argument(const struct function_info *function,
    unsigned int argument)
{
	struct symbol *formal;
	struct symbol *type = function_type(function);
	unsigned int current = 0;

	if (type == NULL)
		return (NULL);
	FOR_EACH_PTR(type->arguments, formal) {
		if (current++ == argument)
			return (formal);
	} END_FOR_EACH_PTR(formal);
	return (NULL);
}

static int
function_access_coordinates(const struct function_info *function,
    const struct locklint_access *access, struct lock_identity_key *key,
    enum lock_analysis_object_type *object_type)
{
	struct symbol *formal;
	unsigned int argument;
	int error;

	error = lock_identity_key_from_access(access, key, object_type);
	if (error != 0)
		return (error);
	if (identity_formal_argument(function, *key, *object_type, &argument) &&
	    (formal = function_formal_argument(function, argument)) != NULL) {
		key->analysis_object = formal;
		*object_type = LOCK_ANALYSIS_OBJECT_SYMBOL;
	}
	return (0);
}

static bool
region_contains(struct visibility_region container,
    struct visibility_region contained)
{
	uint64_t relative;

	if (container.analysis_object != contained.analysis_object ||
	    contained.target_offset < container.target_offset)
		return (false);
	relative = (uint64_t)contained.target_offset -
	    (uint64_t)container.target_offset;
	return (relative <= container.target_length &&
	    contained.target_length <= container.target_length - relative);
}

static bool
map_assumed_key_to_context(const struct function_context *context,
    struct lock_identity_key source,
    enum lock_analysis_object_type source_type,
    struct lock_identity_key *result,
    enum lock_analysis_object_type *result_type)
{
	const struct lock_identity *actual;
	unsigned int argument;

	*result = source;
	*result_type = source_type;
	if (!identity_formal_argument(context->function, source, source_type,
	    &argument))
		return (true);
	actual = binding_environment_lookup(context->bindings, argument);
	if (actual == NULL)
		return (true);
	if ((source.target_offset > 0 &&
	    actual->key.target_offset >
	    INT64_MAX - source.target_offset) ||
	    (source.target_offset < 0 &&
	    actual->key.target_offset <
	    INT64_MIN - source.target_offset))
		die("assumed-region binding offset is out of range");
	result->analysis_object = actual->key.analysis_object;
	result->target_offset =
	    actual->key.target_offset + source.target_offset;
	*result_type = actual->analysis_object_type;
	return (true);
}

static bool
map_assumed_region_to_context(const struct function_context *context,
    const struct assumed_region *assumed, struct visibility_region *region)
{
	struct lock_identity_key key = {
		.analysis_object = assumed->region.analysis_object,
		.target_offset = assumed->region.target_offset
	};
	enum lock_analysis_object_type object_type;

	if (!map_assumed_key_to_context(context, key, assumed->object_type,
	    &key, &object_type))
		return (false);
	(void) object_type;
	*region = assumed->region;
	region->analysis_object = key.analysis_object;
	region->target_offset = key.target_offset;
	return (true);
}

/*
 * Replace a caller-relative formal identity with the corresponding incoming
 * actual identity.  Relative coordinates compose without changing the
 * opaque analysis object.
 */
static bool
compose_caller_identity(const struct function_context *caller,
    struct lock_identity_key *key,
    enum lock_analysis_object_type *object_type)
{
	const struct lock_identity *actual;
	int64_t relative_offset = key->target_offset;
	int64_t actual_offset;
	unsigned int argument;

	if (!identity_formal_argument(caller->function, *key, *object_type,
	    &argument))
		return (false);
	actual = binding_environment_lookup(caller->bindings, argument);
	if (actual == NULL)
		return (false);
	actual_offset = actual->key.target_offset;
	if ((relative_offset > 0 &&
	    actual_offset > INT64_MAX - relative_offset) ||
	    (relative_offset < 0 &&
	    actual_offset < INT64_MIN - relative_offset))
		die("composed binding offset is out of range");
	key->analysis_object = actual->key.analysis_object;
	key->target_offset = actual_offset + relative_offset;
	*object_type = actual->analysis_object_type;
	return (true);
}

static int
context_identity_intern(struct analysis *analysis,
    const struct function_context *context, struct lock_identity_key key,
    enum lock_analysis_object_type object_type,
    struct lock_identity **identity, bool *existed, bool *composed)
{
	*composed = compose_caller_identity(context, &key, &object_type);
	return (lock_identity_intern(analysis->lock_identities, key,
	    object_type, identity, existed));
}

static int
context_access_key(const struct function_context *context,
    const struct locklint_access *access, struct lock_identity_key *key,
    enum lock_analysis_object_type *object_type, bool *composed)
{
	int error;

	error = lock_identity_key_from_access(access, key, object_type);
	if (error != 0)
		return (error);
	*composed = compose_caller_identity(context, key, object_type);
	return (0);
}

/*
 * Translate an access to the canonical object and byte range used by the
 * active caller context.  Unsized accesses remain conservatively visible.
 */
static int
context_access_region(const struct function_context *context,
    const struct locklint_access *access, struct visibility_region *region,
    bool *available, bool *composed)
{
	struct lock_identity_key key;
	enum lock_analysis_object_type object_type;
	struct symbol *formal;
	uint64_t length;
	unsigned int argument;
	int error;

	*available = false;
	*composed = false;
	if (!locklint_access_size(access, &length))
		return (0);
	error = context_access_key(context, access, &key, &object_type,
	    composed);
	if (error != 0)
		return (error);
	if (identity_formal_argument(context->function, key, object_type,
	    &argument) &&
	    binding_environment_lookup(context->bindings, argument) == NULL &&
	    (formal = function_formal_argument(context->function,
	    argument)) != NULL)
		key.analysis_object = formal;
	*region = (struct visibility_region) {
		.analysis_object = key.analysis_object,
		.target_offset = key.target_offset,
		.target_length = length
	};
	*available = true;
	return (0);
}

static int
context_access_identity(struct analysis *analysis,
    const struct function_context *context,
    const struct locklint_access *access, struct lock_identity **identity,
    bool *existed, bool *composed)
{
	struct lock_identity_key key;
	enum lock_analysis_object_type object_type;
	int error;

	error = context_access_key(context, access, &key, &object_type,
	    composed);
	if (error != 0)
		return (error);
	return (lock_identity_intern(analysis->lock_identities, key,
	    object_type, identity, existed));
}

static struct pseudo *
call_argument_pseudo(const struct instruction *insn, unsigned int index)
{
	struct pseudo *pseudo;
	struct pseudo_list *arguments = insn->arguments;
	unsigned int current = 0;

	FOR_EACH_PTR(arguments, pseudo) {
		if (current++ == index)
			return (pseudo);
	} END_FOR_EACH_PTR(pseudo);
	return (NULL);
}

static const struct lock_identity *
call_argument_identity(struct analysis *analysis,
    const struct function_context *caller, const struct instruction *insn,
    unsigned int index)
{
	struct locklint_access access;
	struct lock_identity_key key;
	enum lock_analysis_object_type object_type;
	struct lock_identity *identity;
	struct pseudo *pseudo;
	bool composed;
	bool existed;
	int error;

	if (locklint_get_call_argument_access(caller->function->tu, insn, index,
	    &access)) {
		error = context_access_identity(analysis, caller, &access,
		    &identity, &existed, &composed);
	} else {
		pseudo = call_argument_pseudo(insn, index);
		if (pseudo == NULL)
			die("cannot identify call argument %u", index);
		key.analysis_object = pseudo;
		key.target_offset = 0;
		object_type = LOCK_ANALYSIS_OBJECT_PSEUDO;
		error = context_identity_intern(analysis, caller, key,
		    object_type, &identity, &existed, &composed);
	}
	if (error != 0)
		die("cannot identify call argument %u: %s", index,
		    strerror(error));
	if (composed)
		analysis->counts.binding_identities_composed++;
	if (existed)
		analysis->counts.lock_identities_reused++;
	else
		analysis->counts.lock_identities_created++;
	return (identity);
}

struct assumed_region_builder {
	struct function_info *function;
	const struct instruction *instruction;
};

static void
record_assumed_target(const struct locklint_access *access,
    const struct expression *expr, void *data_arg)
{
	struct assumed_region_builder *data = data_arg;
	struct assumed_region *assumed;
	struct locklint_data_policy policy;
	struct locklint_access protector;
	struct lock_identity_key key;
	enum lock_analysis_object_type object_type;
	uint64_t length;

	(void) expr;
	assumed = calloc(1, sizeof (*assumed));
	if (assumed == NULL)
		die("cannot allocate assumed-protection region");
	assumed->marker = data->instruction;
	if (access != NULL && locklint_access_size(access, &length) &&
	    function_access_coordinates(data->function, access, &key,
	    &object_type) == 0) {
		assumed->valid = true;
		assumed->region = (struct visibility_region) {
			.analysis_object = key.analysis_object,
			.target_offset = key.target_offset,
			.target_length = length
		};
		assumed->object_type = object_type;
		assumed->name = locklint_access_name(access);
		if (locklint_data_policy(access, &policy, &protector) &&
		    policy.protection == LOCKLINT_PROTECTION_MUTEX &&
		    function_access_coordinates(data->function, &protector,
		    &assumed->mutex, &assumed->mutex_object_type) == 0) {
			assumed->has_mutex = true;
			assumed->mutex_name =
			    locklint_access_name(&protector);
		}
	}
	*data->function->assumed_regions_tail = assumed;
	data->function->assumed_regions_tail = &assumed->next;
}

static void
collect_assumed_regions(void)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct basic_block *bb;

		FOR_EACH_PTR(function->ep->bbs, bb) {
			struct instruction *instruction;

			FOR_EACH_PTR(bb->insns, instruction) {
				struct assumed_region_builder data = {
					.function = function,
					.instruction = instruction
				};

				(void) locklint_for_each_assumed_target(
				    function->tu, instruction,
				    record_assumed_target, &data);
			} END_FOR_EACH_PTR(instruction);
		} END_FOR_EACH_PTR(bb);
	}
	callgraph_iter_close(iterator);
}

/*
 * Construct the callee's canonical mapping for pointer formals.  Scalar
 * arguments are values copied into the callee and do not identify caller
 * objects.
 */
static const struct binding_environment *
call_bindings(struct analysis *analysis,
    const struct function_context *caller, struct function_info *callee,
    const struct instruction *insn)
{
	struct symbol *type = function_type(callee);
	struct symbol *formal;
	struct formal_binding *entries;
	struct binding_environment *bindings;
	size_t formal_count = 0;
	size_t binding_count = 0;
	unsigned int argument = 0;
	bool existed;
	int error;

	if (type == NULL)
		die("callee has no function type");
	FOR_EACH_PTR(type->arguments, formal) {
		formal_count++;
	} END_FOR_EACH_PTR(formal);
	if (formal_count > UINT_MAX ||
	    formal_count > SIZE_MAX / sizeof (*entries))
		die("too many formal arguments");
	entries = calloc(formal_count, sizeof (*entries));
	if (entries == NULL && formal_count != 0)
		die("cannot allocate call bindings");
	FOR_EACH_PTR(type->arguments, formal) {
		if (formal_is_pointer(formal)) {
			entries[binding_count].argument = argument;
			entries[binding_count].actual_identity =
			    call_argument_identity(analysis, caller, insn,
			    argument);
			binding_count++;
		}
		argument++;
	} END_FOR_EACH_PTR(formal);
	error = binding_environment_intern(&callee->bindings, entries,
	    binding_count, &bindings, &existed);
	free(entries);
	if (error != 0)
		die("cannot intern call bindings: %s", strerror(error));
	if (existed)
		analysis->counts.binding_environments_reused++;
	else
		analysis->counts.binding_environments_created++;
	return (bindings);
}

static void
process_call(struct analysis *analysis, struct point_state *point_state,
    const struct semantic_state *caller_state)
{
	struct function_context *caller_context = point_state->context;
	const struct binding_environment *bindings;
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
		resume_point = point_state->point;
		resume_point.next_instruction = next_live_instruction(
		    resume_point.block, resume_point.next_instruction);
		record_analysis_point(analysis, caller_context, resume_point,
		    caller_state, false);
		return;
	}

	bindings = call_bindings(analysis, caller_context, callee_function,
	    point_state->point.next_instruction);
	error = context_state_import(callee_function, caller_state,
	    &callee_state, &existed);
	if (error != 0)
		die("cannot import callee state: %s", strerror(error));
	record_semantic_state(analysis, existed);

	error = context_create(callee_function, bindings, callee_state,
	    &callee_context, &context_existed);
	if (error != 0)
		die("cannot create callee context: %s", strerror(error));
	if (context_existed) {
		analysis->counts.contexts_reused++;
	} else {
		analysis->counts.contexts_created++;
		if (context_count(callee_function) == 1)
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

	resume_point = point_state->point;
	resume_point.next_instruction = next_live_instruction(
	    point_state->point.block, point_state->point.next_instruction);
	error = dependency_continuation_create(callee_context, caller_context,
	    resume_point, caller_state, bindings, &continuation, &existed);
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
		    callee_state, false);
	}
	record_reactivation(analysis, continuation);
}

static struct instruction *
conditional_result_branch(struct basic_block *block,
    const struct instruction *operation)
{
	struct instruction *instruction;

	FOR_EACH_PTR(block->insns, instruction) {
		if (instruction->bb != NULL && instruction->opcode == OP_CBR &&
		    instruction->cond != NULL &&
		    instruction->cond->type == PSEUDO_REG &&
		    instruction->cond->def == operation)
			return (instruction);
	} END_FOR_EACH_PTR(instruction);
	return (NULL);
}

static void
record_conditional_outcome(struct analysis *analysis,
    struct point_state *point_state, struct analysis_point next,
    const struct instruction *branch, bool nonzero,
    const struct semantic_state *state)
{
	if (branch != NULL) {
		next.conditional_instruction =
		    point_state->point.next_instruction;
		next.conditional_nonzero = nonzero;
	}
	record_analysis_point(analysis, point_state->context, next, state,
	    false);
}

/*
 * Preserve the possible results of a conditional lock operation.  A result
 * consumed by this block's branch remains tagged until that edge is selected;
 * otherwise both states continue without a condition.
 */
static bool
process_conditional_lock(struct analysis *analysis,
    struct point_state *point_state)
{
	struct instruction *instruction = point_state->point.next_instruction;
	struct locklint_access access;
	struct lock_identity *identity;
	struct instruction *branch;
	struct semantic_state *success_state;
	struct analysis_point next = point_state->point;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;
	unsigned int current_modes;
	bool composed;
	bool existed;
	int error;

	action = locklint_get_lock_action(point_state->context->function->tu,
	    instruction, &access, &mode);
	if (action != LOCKLINT_LOCK_TRY_ACQUIRE &&
	    action != LOCKLINT_LOCK_TRY_UPGRADE &&
	    action != LOCKLINT_LOCK_TRY_ACQUIRE_ZERO &&
	    action != LOCKLINT_LOCK_RESULT_ACQUIRE)
		return (false);
	branch = conditional_result_branch(point_state->point.block,
	    instruction);
	if (action == LOCKLINT_LOCK_RESULT_ACQUIRE && branch != NULL)
		return (false);
	next.next_instruction =
	    next_live_instruction(point_state->point.block, instruction);
	if (access.root == NULL) {
		analysis->counts.lock_identities_unresolved++;
		record_analysis_point(analysis, point_state->context, next,
		    point_state->state, false);
		return (true);
	}
	error = context_access_identity(analysis, point_state->context, &access,
	    &identity, &existed, &composed);
	if (error != 0)
		die("cannot identify conditional lock operation: %s",
		    strerror(error));
	if (composed)
		analysis->counts.binding_identities_composed++;
	if (existed)
		analysis->counts.lock_identities_reused++;
	else
		analysis->counts.lock_identities_created++;

	current_modes = context_state_lock_modes(point_state->state, identity);
	if (action == LOCKLINT_LOCK_RESULT_ACQUIRE) {
		error = context_state_set_lock(point_state->context->function,
		    point_state->state, identity, LOCKLINT_MODE_MUTEX,
		    &success_state, &existed);
		if (error != 0)
			die("cannot apply mutex lock operation: %s",
			    strerror(error));
		record_semantic_state(analysis, existed);
		record_analysis_point(analysis, point_state->context, next,
		    success_state, false);
		analysis->counts.lock_transitions_applied++;
		return (true);
	}
	if (action == LOCKLINT_LOCK_TRY_ACQUIRE_ZERO) {
		error = context_state_set_lock(point_state->context->function,
		    point_state->state, identity, LOCKLINT_MODE_MUTEX,
		    &success_state, &existed);
		if (error != 0)
			die("cannot apply conditional lock operation: %s",
			    strerror(error));
		record_semantic_state(analysis, existed);
		record_conditional_outcome(analysis, point_state, next, branch,
		    false, success_state);
		record_conditional_outcome(analysis, point_state, next, branch,
		    true, success_state);
		record_conditional_outcome(analysis, point_state, next, branch,
		    true, point_state->state);
		analysis->counts.lock_transitions_applied++;
		return (true);
	}
	if ((action == LOCKLINT_LOCK_TRY_UPGRADE &&
	    (current_modes & LOCKLINT_MODE_READER) != 0) ||
	    (action == LOCKLINT_LOCK_TRY_ACQUIRE && current_modes == 0)) {
		error = context_state_set_lock(point_state->context->function,
		    point_state->state, identity,
		    action == LOCKLINT_LOCK_TRY_UPGRADE ?
		    LOCKLINT_MODE_WRITER : mode, &success_state, &existed);
		if (error != 0)
			die("cannot apply conditional lock operation: %s",
			    strerror(error));
		record_semantic_state(analysis, existed);
		record_conditional_outcome(analysis, point_state, next, branch,
		    true, success_state);
	}
	record_conditional_outcome(analysis, point_state, next, branch, false,
	    point_state->state);
	analysis->counts.lock_transitions_applied++;
	return (true);
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
			const struct semantic_state *state;

			if (process_conditional_lock(analysis, point_state))
				return;
			state = apply_lock_event(analysis, point_state);

			process_call(analysis, point_state, state);
			return;
		}
		point.next_instruction =
		    next_live_instruction(point.block, point.next_instruction);
		record_analysis_point(analysis, point_state->context, point,
		    apply_visibility_event(analysis, point_state), false);
		return;
	}

	{
		struct basic_block *child;
		struct instruction *branch = NULL;

		if (point.conditional_instruction != NULL)
			branch = conditional_result_branch(point.block,
			    point.conditional_instruction);

		FOR_EACH_PTR(point.block->children, child) {
			struct analysis_point child_point = {
				.block = child,
				.next_instruction = first_live_instruction(child)
			};

			if (branch != NULL &&
			    ((child == branch->bb_true) !=
			    point.conditional_nonzero))
				continue;
			if (branch == NULL) {
				child_point.conditional_instruction =
				    point.conditional_instruction;
				child_point.conditional_nonzero =
				    point.conditional_nonzero;
			}
			record_analysis_point(analysis, point_state->context,
			    child_point, point_state->state,
			    domtree_dominates(child, point.block));
		} END_FOR_EACH_PTR(child);
	}
}

static void
seed_root(struct analysis *analysis, struct function_info *function)
{
	struct binding_environment *bindings;
	struct function_context *context;
	struct semantic_state *state;
	bool existed;
	int error;

	analysis->counts.roots++;
	error = binding_environment_intern(&function->bindings, NULL, 0,
	    &bindings, &existed);
	if (error != 0)
		die("cannot intern root bindings: %s", strerror(error));
	if (existed)
		analysis->counts.binding_environments_reused++;
	else
		analysis->counts.binding_environments_created++;
	error = context_entry_state_intern(function, &state, &existed);
	if (error != 0)
		die("cannot intern root state: %s", strerror(error));
	record_semantic_state(analysis, existed);

	error = context_create(function, bindings, state, &context, &existed);
	if (error != 0)
		die("cannot create root context: %s", strerror(error));
	if (existed) {
		analysis->counts.contexts_reused++;
	} else {
		analysis->counts.contexts_created++;
		if (context_count(function) == 1)
			analysis->counts.functions++;
	}
	record_point(analysis, context, function->ep->entry->bb,
	    first_live_instruction(function->ep->entry->bb), state, false);
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

/*
 * Diagnose one instruction after all exact states reaching it are known.
 * Mixed valid and invalid states are left for later "maybe" diagnostics.
 */
static struct point_state *
diagnose_lock_transition(struct analysis *analysis,
    struct function_context *context, struct point_state *first)
{
	struct instruction *insn = first->point.next_instruction;
	struct locklint_access access;
	struct lock_identity *identity;
	struct point_state *point_state;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;
	size_t invalid = 0;
	size_t uncertain = 0;
	size_t valid = 0;
	bool composed;
	bool existed;
	int error;

	action = locklint_get_lock_action(context->function->tu, insn, &access,
	    &mode);
	if ((action == LOCKLINT_LOCK_ACQUIRE ||
	    action == LOCKLINT_LOCK_RELEASE ||
	    action == LOCKLINT_LOCK_DOWNGRADE ||
	    action == LOCKLINT_LOCK_TRY_UPGRADE) && access.root != NULL) {
		error = context_access_identity(analysis, context, &access,
		    &identity, &existed, &composed);
		if (error != 0)
			die("cannot identify lock diagnostic: %s",
			    strerror(error));
		if (!existed)
			die("lock diagnostic identity was not observed");
	}
	for (point_state = first; point_state != NULL &&
	    same_analysis_point(first, point_state);
	    point_state = AVL_NEXT(&context->point_states, point_state)) {
		unsigned int current_modes;

		if ((action != LOCKLINT_LOCK_ACQUIRE &&
		    action != LOCKLINT_LOCK_RELEASE &&
		    action != LOCKLINT_LOCK_DOWNGRADE &&
		    action != LOCKLINT_LOCK_TRY_UPGRADE) ||
		    access.root == NULL)
			continue;
		current_modes = context_state_lock_modes(point_state->state,
		    identity);
		if (action == LOCKLINT_LOCK_DOWNGRADE &&
		    current_modes == LOCKLINT_MODE_WRITER) {
			valid++;
		} else if (action == LOCKLINT_LOCK_DOWNGRADE &&
		    (current_modes & LOCKLINT_MODE_WRITER) != 0) {
			uncertain++;
		} else if (action == LOCKLINT_LOCK_TRY_UPGRADE &&
		    current_modes == LOCKLINT_MODE_READER) {
			valid++;
		} else if (action == LOCKLINT_LOCK_TRY_UPGRADE &&
		    (current_modes & LOCKLINT_MODE_READER) != 0) {
			uncertain++;
		} else if ((action == LOCKLINT_LOCK_ACQUIRE &&
		    current_modes != 0) ||
		    (action == LOCKLINT_LOCK_RELEASE && current_modes == 0) ||
		    action == LOCKLINT_LOCK_DOWNGRADE ||
		    action == LOCKLINT_LOCK_TRY_UPGRADE) {
			invalid++;
		} else {
			valid++;
		}
	}
	if (invalid != 0 || uncertain != 0) {
		char *name = locklint_access_name(&access);
		struct position pos = insn->call_expr != NULL ?
		    insn->call_expr->pos : insn->pos;

		if (action == LOCKLINT_LOCK_TRY_UPGRADE) {
			if (valid == 0 && uncertain == 0) {
				locklint_warning(
				    LOCKLINT_DIAG_LOCK_NOT_READ_HELD, pos,
				    "lock '%s' is not read-held", name);
			} else {
				locklint_warning(
				    LOCKLINT_DIAG_LOCK_MAYBE_NOT_READ_HELD,
				    pos, "lock '%s' may not be read-held",
				    name);
			}
		} else if (action == LOCKLINT_LOCK_DOWNGRADE) {
			if (valid == 0 && uncertain == 0) {
				locklint_warning(
				    LOCKLINT_DIAG_LOCK_NOT_WRITE_HELD, pos,
				    "lock '%s' is not write-held", name);
			} else {
				locklint_warning(
				    LOCKLINT_DIAG_LOCK_MAYBE_NOT_WRITE_HELD,
				    pos, "lock '%s' may not be write-held",
				    name);
			}
		} else if (action == LOCKLINT_LOCK_ACQUIRE) {
			if (valid == 0) {
				locklint_warning(
				    LOCKLINT_DIAG_LOCK_ALREADY_HELD, pos,
				    "lock '%s' is already held", name);
			} else {
				locklint_warning(
				    LOCKLINT_DIAG_LOCK_MAYBE_ALREADY_HELD, pos,
				    "lock '%s' may already be held", name);
			}
		} else {
			if (valid == 0) {
				locklint_warning(LOCKLINT_DIAG_LOCK_NOT_HELD, pos,
				    "lock '%s' is not held", name);
			} else {
				locklint_warning(
				    LOCKLINT_DIAG_LOCK_MAYBE_NOT_HELD, pos,
				    "lock '%s' may not be held", name);
			}
		}
		free(name);
	}
	return (point_state);
}

static void
diagnose_lock_transitions(struct analysis *analysis)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct function_context *context;

		for (context = avl_first(&function->contexts.contexts);
		    context != NULL;
		    context = AVL_NEXT(&function->contexts.contexts, context)) {
			struct point_state *point_state =
			    avl_first(&context->point_states);

			while (point_state != NULL) {
				if (point_state->point.next_instruction == NULL) {
					point_state = AVL_NEXT(
					    &context->point_states, point_state);
					continue;
				}
				point_state = diagnose_lock_transition(analysis,
				    context, point_state);
			}
		}
	}
	callgraph_iter_close(iterator);
}

struct competition_underflow_finding {
	struct instruction *instruction;
	bool definite;
	bool maybe;
	avl_node_t by_instruction;
};

static int
compare_competition_underflow(const void *left_arg, const void *right_arg)
{
	const struct competition_underflow_finding *left = left_arg;
	const struct competition_underflow_finding *right = right_arg;

	return (AVL_PCMP(left->instruction, right->instruction));
}

static void
diagnose_competition_underflow(void)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct competition_underflow_finding *finding;
		struct function_context *context;
		avl_tree_t findings;

		avl_create(&findings, compare_competition_underflow,
		    sizeof (struct competition_underflow_finding),
		    offsetof(struct competition_underflow_finding,
		    by_instruction));
		for (context = avl_first(&function->contexts.contexts);
		    context != NULL;
		    context = AVL_NEXT(&function->contexts.contexts, context)) {
			struct point_state *point_state;

			for (point_state = avl_first(&context->point_states);
			    point_state != NULL;
			    point_state = AVL_NEXT(&context->point_states,
			    point_state)) {
				struct competition_interval competition;
				struct competition_underflow_finding key;
				struct instruction *instruction;
				avl_index_t where;

				instruction =
				    point_state->point.next_instruction;
				if (instruction == NULL ||
				    locklint_get_execution_annotation(instruction) !=
				    LOCKLINT_EXECUTION_NO_COMPETITION)
					continue;
				competition =
				    context_state_competition(point_state->state);
				if (competition.entry_condition ||
				    (!competition.minimum_unbounded &&
				    competition.minimum > 0))
					continue;
				key = (struct competition_underflow_finding) {
					.instruction = instruction
				};
				finding = avl_find(&findings, &key, &where);
				if (finding == NULL) {
					finding = calloc(1, sizeof (*finding));
					if (finding == NULL)
						die("cannot allocate competition "
						    "underflow finding");
					finding->instruction = instruction;
					avl_insert(&findings, finding, where);
				}
				if (!competition.maximum_unbounded &&
				    competition.maximum <= 0)
					finding->definite = true;
				else
					finding->maybe = true;
			}
		}
		while ((finding = avl_first(&findings)) != NULL) {
			if (finding->definite) {
				locklint_warning(
				    LOCKLINT_DIAG_COMPETITION_UNDERFLOW,
				    finding->instruction->pos,
				    "competition depth decremented below zero");
			} else if (finding->maybe) {
				locklint_warning(
				    LOCKLINT_DIAG_COMPETITION_MAYBE_UNDERFLOW,
				    finding->instruction->pos,
				    "competition depth may be decremented "
				    "below zero");
			}
			avl_remove(&findings, finding);
			free(finding);
		}
		avl_destroy(&findings);
	}
	callgraph_iter_close(iterator);
}

static bool
same_competition_interval(struct competition_interval left,
    struct competition_interval right)
{
	return (left.minimum == right.minimum &&
	    left.maximum == right.maximum &&
	    left.minimum_unbounded == right.minimum_unbounded &&
	    left.maximum_unbounded == right.maximum_unbounded &&
	    left.entry_condition == right.entry_condition);
}

static void
diagnose_competition_declaration(struct function_info *function,
    struct instruction *instruction, int adjustment)
{
	struct function_context *context;
	bool definite = false;
	bool conditional = false;

	for (context = avl_first(&function->contexts.contexts);
	    context != NULL;
	    context = AVL_NEXT(&function->contexts.contexts, context)) {
		struct competition_interval expected;
		struct context_exit *exit;
		size_t valid = 0;
		size_t invalid = 0;
		int error;

		error = context_competition_adjust(
		    context_state_competition(context->entry_state), adjustment,
		    &expected);
		if (error != 0) {
			invalid++;
		} else {
			SLIST_FOREACH(exit, &context->exits, link) {
				if (same_competition_interval(
				    context_state_competition(exit->state),
				    expected))
					valid++;
				else
					invalid++;
			}
			if (SLIST_EMPTY(&context->exits))
				invalid++;
		}
		if (invalid != 0 && valid == 0)
			definite = true;
		else if (invalid != 0)
			conditional = true;
	}
	if (definite) {
		locklint_warning(LOCKLINT_DIAG_DECLARED_COMPETITION_EFFECT,
		    instruction->pos, "function '%s' does not establish "
		    "declared competition-depth %s", function_name(function),
		    adjustment > 0 ? "increase" : "decrease");
	} else if (conditional) {
		locklint_warning(LOCKLINT_DIAG_DECLARED_COMPETITION_EFFECT,
		    instruction->pos, "declared competition-depth %s is not "
		    "established on every return from '%s'",
		    adjustment > 0 ? "increase" : "decrease",
		    function_name(function));
	}
}

static void
diagnose_declared_competition_effects(void)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct basic_block *block;

		FOR_EACH_PTR(function->ep->bbs, block) {
			struct instruction *instruction;

			FOR_EACH_PTR(block->insns, instruction) {
				enum locklint_execution_kind kind;

				if (instruction->bb == NULL)
					continue;
				kind = locklint_get_execution_annotation(
				    instruction);
				if (kind ==
				    LOCKLINT_EXECUTION_COMPETITION_EFFECT) {
					diagnose_competition_declaration(function,
					    instruction, 1);
				} else if (kind ==
				    LOCKLINT_EXECUTION_NO_COMPETITION_EFFECT) {
					diagnose_competition_declaration(function,
					    instruction, -1);
				}
			} END_FOR_EACH_PTR(instruction);
		} END_FOR_EACH_PTR(block);
	}
	callgraph_iter_close(iterator);
}

struct competition_assertion_finding {
	struct instruction *instruction;
	bool definite;
	bool maybe;
	avl_node_t by_instruction;
};

static int
compare_competition_assertion(const void *left_arg, const void *right_arg)
{
	const struct competition_assertion_finding *left = left_arg;
	const struct competition_assertion_finding *right = right_arg;

	return (AVL_PCMP(left->instruction, right->instruction));
}

/*
 * Validate each assertion against every reached competition state without
 * using the assertion to refine state for subsequent instructions.
 */
static void
diagnose_competition_assertions(void)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct competition_assertion_finding *finding;
		struct function_context *context;
		avl_tree_t findings;

		avl_create(&findings, compare_competition_assertion,
		    sizeof (struct competition_assertion_finding),
		    offsetof(struct competition_assertion_finding,
		    by_instruction));
		for (context = avl_first(&function->contexts.contexts);
		    context != NULL;
		    context = AVL_NEXT(&function->contexts.contexts, context)) {
			struct point_state *point_state;

			for (point_state = avl_first(&context->point_states);
			    point_state != NULL;
			    point_state = AVL_NEXT(&context->point_states,
			    point_state)) {
				struct competition_interval competition;
				struct competition_assertion_finding key;
				struct instruction *instruction;
				avl_index_t where;

				instruction =
				    point_state->point.next_instruction;
				if (instruction == NULL ||
				    locklint_get_execution_annotation(instruction) !=
				    LOCKLINT_EXECUTION_ASSERT_NO_COMPETITION)
					continue;
				competition =
				    context_state_competition(point_state->state);
				if (!competition.maximum_unbounded &&
				    competition.maximum <= 0)
					continue;
				key = (struct competition_assertion_finding) {
					.instruction = instruction
				};
				finding = avl_find(&findings, &key, &where);
				if (finding == NULL) {
					finding = calloc(1, sizeof (*finding));
					if (finding == NULL)
						die("cannot allocate competition "
						    "assertion finding");
					finding->instruction = instruction;
					avl_insert(&findings, finding, where);
				}
				if (!competition.minimum_unbounded &&
				    competition.minimum > 0)
					finding->definite = true;
				else
					finding->maybe = true;
			}
		}
		while ((finding = avl_first(&findings)) != NULL) {
			if (finding->definite) {
				locklint_warning(
				    LOCKLINT_DIAG_ASSERTED_COMPETITION_REQUIREMENT,
				    finding->instruction->pos,
				    "competing threads exist at "
				    "NO_COMPETING_THREADS assertion");
			} else if (finding->maybe) {
				locklint_warning(
				    LOCKLINT_DIAG_CONDITIONAL_ASSERTED_COMPETITION_REQUIREMENT,
				    finding->instruction->pos,
				    "competing threads may exist at "
				    "NO_COMPETING_THREADS assertion");
			}
			avl_remove(&findings, finding);
			free(finding);
		}
		avl_destroy(&findings);
	}
	callgraph_iter_close(iterator);
}

struct protected_access_diagnostic {
	struct analysis *analysis;
	struct function_context *context;
	struct point_state *first;
	struct instruction *instruction;
	avl_tree_t *findings;
};

struct protected_access_finding {
	struct instruction *instruction;
	const struct locklint_member_path *path;
	struct locklint_access access;
	struct locklint_access protector;
	enum locklint_protection protection;
	unsigned int required_modes;
	unsigned int observed_modes;
	bool unprotected;
	bool conditional;
	bool read_only_visible;
	bool read_only_maybe_visible;
	avl_node_t by_access;
};

static int
compare_protected_access_finding(const void *left_arg, const void *right_arg)
{
	const struct protected_access_finding *left = left_arg;
	const struct protected_access_finding *right = right_arg;
	int order;

	order = AVL_PCMP(left->instruction, right->instruction);
	if (order != 0)
		return (order);
	return (AVL_PCMP(left->path, right->path));
}

/*
 * Check one policy-bearing leaf against every exact state reaching its
 * instruction.  Rwlock reads accept either held mode, while writes require
 * writer ownership.  Lock protection and read-only status remain independent:
 * holding a lock does not permit modifying read-only data during competition.
 */
static void
diagnose_protected_leaf(const struct locklint_access *access, void *data_arg)
{
	struct protected_access_diagnostic *data = data_arg;
	struct locklint_data_policy policy;
	struct locklint_access protector;
	struct lock_identity *identity;
	struct visibility_region region;
	struct point_state *point_state;
	size_t unprotected = 0;
	size_t protected = 0;
	size_t conditional = 0;
	size_t read_only_visible = 0;
	size_t read_only_hidden = 0;
	size_t read_only_maybe_visible = 0;
	unsigned int required_modes = 0;
	unsigned int observed_modes = 0;
	bool check_lock;
	bool check_read_only;
	bool composed;
	bool existed;
	bool region_available;
	int error;

	if (!locklint_data_policy(access, &policy, &protector))
		return;
	check_lock = (policy.protection == LOCKLINT_PROTECTION_MUTEX ||
	    policy.protection == LOCKLINT_PROTECTION_RWLOCK) &&
	    !(data->instruction->opcode == OP_LOAD &&
	    policy.readable_without_lock);
	if (check_lock) {
		if (policy.protection == LOCKLINT_PROTECTION_MUTEX)
			required_modes = LOCKLINT_MODE_MUTEX;
		else if (data->instruction->opcode == OP_LOAD)
			required_modes =
			    LOCKLINT_MODE_READER | LOCKLINT_MODE_WRITER;
		else
			required_modes = LOCKLINT_MODE_WRITER;
	}
	check_read_only = policy.read_only &&
	    data->instruction->opcode == OP_STORE;
	if (!check_lock && !check_read_only)
		return;
	error = context_access_region(data->context, access, &region,
	    &region_available, &composed);
	if (error != 0)
		die("cannot identify protected access region: %s",
		    strerror(error));
	if (composed)
		data->analysis->counts.binding_identities_composed++;
	if (region_available) {
		struct assumed_region *assumed;

		for (assumed = data->context->function->assumed_regions;
		    assumed != NULL; assumed = assumed->next) {
			struct visibility_region mapped;

			if (assumed->valid &&
			    map_assumed_region_to_context(data->context,
			    assumed, &mapped) &&
			    region_contains(mapped, region))
				return;
		}
	}
	if (check_lock) {
		error = context_access_identity(data->analysis, data->context,
		    &protector, &identity, &existed, &composed);
		if (error != 0)
			die("cannot identify data protector: %s",
			    strerror(error));
		if (composed)
			data->analysis->counts.binding_identities_composed++;
		if (existed)
			data->analysis->counts.lock_identities_reused++;
		else
			data->analysis->counts.lock_identities_created++;
	}
	for (point_state = data->first; point_state != NULL &&
	    same_analysis_point(data->first, point_state);
	    point_state = AVL_NEXT(&data->context->point_states, point_state)) {
		struct competition_interval competition =
		    context_state_competition(point_state->state);
		enum semantic_visibility visibility =
		    SEMANTIC_VISIBILITY_VISIBLE;
		bool invisible;

		if (region_available) {
			(void) context_state_effective_visibility(
			    point_state->state, region, &visibility);
		}
		invisible = visibility == SEMANTIC_VISIBILITY_INVISIBLE;

		if (check_lock) {
			unsigned int modes =
			    context_state_lock_modes(point_state->state,
			    identity);

			if (invisible ||
			    (modes & required_modes) != 0 ||
			    (!competition.maximum_unbounded &&
			    competition.maximum <= 0)) {
				protected++;
			} else if (competition.entry_condition ||
			    (!competition.minimum_unbounded &&
			    competition.minimum > 0)) {
				unprotected++;
				observed_modes |= modes;
			} else {
				conditional++;
				observed_modes |= modes;
			}
		}
		if (check_read_only) {
			if (invisible ||
			    (!competition.maximum_unbounded &&
			    competition.maximum <= 0)) {
				read_only_hidden++;
			} else if (!competition.minimum_unbounded &&
			    competition.minimum > 0) {
				read_only_visible++;
			} else {
				read_only_maybe_visible++;
			}
		}
	}
	if (unprotected != 0 || conditional != 0 ||
	    read_only_visible != 0 || read_only_maybe_visible != 0) {
		struct protected_access_finding lookup = {
			.instruction = data->instruction,
			.path = access->path
		};
		struct protected_access_finding *finding;
		avl_index_t where;

		finding = avl_find(data->findings, &lookup, &where);
		if (finding == NULL) {
			finding = calloc(1, sizeof (*finding));
			if (finding == NULL)
				die("cannot allocate protected access finding");
			finding->instruction = data->instruction;
			finding->path = access->path;
			finding->access = *access;
			finding->protector = protector;
			finding->protection = policy.protection;
			finding->required_modes = required_modes;
			avl_insert(data->findings, finding, where);
		}
		finding->observed_modes |= observed_modes;
		if (unprotected != 0 && protected == 0 && conditional == 0)
			finding->unprotected = true;
		else if (unprotected != 0 || conditional != 0)
			finding->conditional = true;
		if (read_only_visible != 0 && read_only_hidden == 0 &&
		    read_only_maybe_visible == 0)
			finding->read_only_visible = true;
		else if (read_only_visible != 0 ||
		    read_only_maybe_visible != 0)
			finding->read_only_maybe_visible = true;
	}
}

static struct point_state *
diagnose_protected_access(struct analysis *analysis,
    struct function_context *context, struct point_state *first,
    avl_tree_t *findings)
{
	struct instruction *instruction = first->point.next_instruction;
	struct point_state *next;

	for (next = first; next != NULL && same_analysis_point(first, next);
	    next = AVL_NEXT(&context->point_states, next))
		;
	if (instruction->opcode == OP_LOAD || instruction->opcode == OP_STORE) {
		struct protected_access_diagnostic data = {
			.analysis = analysis,
			.context = context,
			.first = first,
			.instruction = instruction,
			.findings = findings
		};

		locklint_for_each_instruction_leaf_access(context->function->tu,
		    instruction, diagnose_protected_leaf, &data);
	}
	return (next);
}

/*
 * Explain which observed rwlock mode failed an access requirement.  Exact
 * contributors distinguish a definite mismatch from a mode seen on only
 * some paths.
 */
static void
emit_rwlock_mode_info(const struct protected_access_finding *finding,
    struct position pos, const char *lock, bool conditional)
{
	const char *held;
	const char *required;

	if (finding->observed_modes == LOCKLINT_MODE_READER)
		held = "read-held";
	else if (finding->observed_modes == LOCKLINT_MODE_WRITER)
		held = "write-held";
	else if ((finding->observed_modes &
	    (LOCKLINT_MODE_READER | LOCKLINT_MODE_WRITER)) != 0)
		held = "held in multiple modes";
	else
		held = "held in an incompatible mode";
	required = finding->required_modes == LOCKLINT_MODE_WRITER ?
	    "write" : "read";
	if (conditional) {
		info(pos, "locklint: required lock '%s' may be %s; "
		    "%s-holding is required at this protected access",
		    lock, held, required);
	} else {
		info(pos, "locklint: required lock '%s' is %s; "
		    "%s-holding is required at this protected access",
		    lock, held, required);
	}
}

static void
emit_protected_access_findings(avl_tree_t *findings)
{
	struct protected_access_finding *finding;

	while ((finding = avl_first(findings)) != NULL) {
		char *member = locklint_access_name(&finding->access);
		struct position pos = finding->instruction->access != NULL ?
		    finding->instruction->access->pos :
		    finding->instruction->pos;

		if (finding->read_only_visible) {
			locklint_warning(LOCKLINT_DIAG_READ_ONLY_VISIBLE, pos,
			    "read-only data '%s' modified while visible to "
			    "competing threads", member);
		} else if (finding->read_only_maybe_visible) {
			locklint_warning(LOCKLINT_DIAG_READ_ONLY_MAYBE_VISIBLE,
			    pos, "read-only data '%s' may be modified while "
			    "visible to competing threads", member);
		}
		if (finding->unprotected) {
			char *lock =
			    locklint_access_name(&finding->protector);
			const char *holding =
			    finding->protection == LOCKLINT_PROTECTION_RWLOCK ?
			    (finding->instruction->opcode == OP_LOAD ?
			    "read-holding" : "write-holding") : "holding";

			locklint_warning(LOCKLINT_DIAG_UNPROTECTED_ACCESS, pos,
			    "protected member '%s' %s without %s '%s'",
			    member, finding->instruction->opcode == OP_LOAD ?
			    "read" : "modified", holding, lock);
			if (finding->protection ==
			    LOCKLINT_PROTECTION_RWLOCK &&
			    finding->observed_modes != 0)
				emit_rwlock_mode_info(finding, pos, lock, false);
			free(lock);
		} else if (finding->conditional) {
			locklint_warning(LOCKLINT_DIAG_CONDITIONAL_PROTECTION,
			    pos, "protection for member '%s' is not "
			    "established on every path", member);
			if (finding->protection ==
			    LOCKLINT_PROTECTION_RWLOCK &&
			    finding->observed_modes != 0) {
				char *lock =
				    locklint_access_name(&finding->protector);

				emit_rwlock_mode_info(finding, pos, lock, true);
				free(lock);
			}
		}
		free(member);
		avl_remove(findings, finding);
		free(finding);
	}
}

static void
diagnose_protected_accesses(struct analysis *analysis)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct function_context *context;
		avl_tree_t findings;

		avl_create(&findings, compare_protected_access_finding,
		    sizeof (struct protected_access_finding),
		    offsetof(struct protected_access_finding, by_access));
		for (context = avl_first(&function->contexts.contexts);
		    context != NULL;
		    context = AVL_NEXT(&function->contexts.contexts, context)) {
			struct point_state *point_state =
			    avl_first(&context->point_states);

			while (point_state != NULL) {
				if (point_state->point.next_instruction == NULL) {
					point_state = AVL_NEXT(
					    &context->point_states, point_state);
					continue;
				}
				point_state = diagnose_protected_access(analysis,
				    context, point_state, &findings);
			}
		}
		emit_protected_access_findings(&findings);
		avl_destroy(&findings);
	}
	callgraph_iter_close(iterator);
}

struct assumed_call_finding {
	const struct instruction *instruction;
	const struct function_info *callee;
	const struct assumed_region *assumed;
	bool satisfied;
	bool definite_failure;
	bool conditional_failure;
	struct assumed_call_finding *next;
};

static bool
map_assumed_key_at_call(struct analysis *analysis,
    const struct function_context *caller, const struct function_info *callee,
    const struct instruction *instruction, struct lock_identity_key source,
    enum lock_analysis_object_type source_type, bool normalize_formal,
    struct lock_identity_key *result,
    enum lock_analysis_object_type *result_type)
{
	const struct lock_identity *actual;
	struct symbol *formal;
	unsigned int argument;
	unsigned int caller_argument;

	*result = source;
	*result_type = source_type;
	if (!identity_formal_argument(callee, source, source_type, &argument))
		return (true);
	actual = call_argument_identity(analysis, caller, instruction, argument);
	if ((source.target_offset > 0 &&
	    actual->key.target_offset >
	    INT64_MAX - source.target_offset) ||
	    (source.target_offset < 0 &&
	    actual->key.target_offset <
	    INT64_MIN - source.target_offset))
		die("mapped assumed-region offset is out of range");
	result->analysis_object = actual->key.analysis_object;
	result->target_offset =
	    actual->key.target_offset + source.target_offset;
	*result_type = actual->analysis_object_type;
	if (normalize_formal &&
	    identity_formal_argument(caller->function, actual->key,
	    actual->analysis_object_type, &caller_argument) &&
	    binding_environment_lookup(caller->bindings, caller_argument) ==
	    NULL && (formal = function_formal_argument(caller->function,
	    caller_argument)) != NULL) {
		result->analysis_object = formal;
		*result_type = LOCK_ANALYSIS_OBJECT_SYMBOL;
	}
	return (true);
}

static struct assumed_call_finding *
assumed_call_finding(struct assumed_call_finding ***tail,
    struct assumed_call_finding *findings, const struct instruction *instruction,
    const struct function_info *callee, const struct assumed_region *assumed)
{
	struct assumed_call_finding *finding;

	for (finding = findings; finding != NULL; finding = finding->next) {
		if (finding->instruction == instruction &&
		    finding->assumed == assumed)
			return (finding);
	}
	finding = calloc(1, sizeof (*finding));
	if (finding == NULL)
		die("cannot allocate assumed-protection call finding");
	finding->instruction = instruction;
	finding->callee = callee;
	finding->assumed = assumed;
	**tail = finding;
	*tail = &finding->next;
	return (finding);
}

static void
check_assumed_call_state(struct analysis *analysis,
    const struct function_context *caller, const struct instruction *instruction,
    const struct semantic_state *state, const struct function_info *callee,
    const struct assumed_region *assumed,
    struct assumed_call_finding *finding)
{
	struct lock_identity_key key = {
		.analysis_object = assumed->region.analysis_object,
		.target_offset = assumed->region.target_offset
	};
	struct visibility_region region;
	struct competition_interval competition;
	enum lock_analysis_object_type object_type;
	enum semantic_visibility visibility = SEMANTIC_VISIBILITY_VISIBLE;
	bool protected = false;

	if (!map_assumed_key_at_call(analysis, caller, callee, instruction,
	    key, assumed->object_type, true, &key, &object_type))
		return;
	(void) object_type;
	region = assumed->region;
	region.analysis_object = key.analysis_object;
	region.target_offset = key.target_offset;
	(void) context_state_effective_visibility(state, region, &visibility);
	if (visibility == SEMANTIC_VISIBILITY_INVISIBLE)
		protected = true;
	if (!protected && assumed->has_mutex) {
		struct lock_identity *mutex;
		struct lock_identity_key mutex_key;
		enum lock_analysis_object_type mutex_type;
		bool existed;
		int error;

		if (!map_assumed_key_at_call(analysis, caller, callee,
		    instruction, assumed->mutex, assumed->mutex_object_type,
		    false, &mutex_key, &mutex_type))
			return;
		error = lock_identity_intern(analysis->lock_identities,
		    mutex_key, mutex_type, &mutex, &existed);
		if (error != 0)
			die("cannot identify assumed-protection mutex: %s",
			    strerror(error));
		if (existed)
			analysis->counts.lock_identities_reused++;
		else
			analysis->counts.lock_identities_created++;
		if ((context_state_lock_modes(state, mutex) &
		    LOCKLINT_MODE_MUTEX) != 0)
			protected = true;
	}
	competition = context_state_competition(state);
	if (!protected && !competition.maximum_unbounded &&
	    competition.maximum <= 0)
		protected = true;
	if (protected) {
		finding->satisfied = true;
	} else if (competition.entry_condition ||
	    (!competition.minimum_unbounded && competition.minimum > 0)) {
		finding->definite_failure = true;
	} else {
		finding->conditional_failure = true;
	}
}

/*
 * Validate every exact state at each resolved call.  Findings are aggregated
 * only for reporting: one satisfied state makes other failures conditional,
 * matching the existing protected-access convention.
 */
static void
diagnose_assumed_calls(struct analysis *analysis)
{
	struct assumed_call_finding *findings = NULL;
	struct assumed_call_finding **tail = &findings;
	struct callgraph_iter *iterator;
	struct function_info *caller;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((caller = callgraph_iter_next(iterator)) != NULL) {
		struct function_context *context;

		for (context = avl_first(&caller->contexts.contexts);
		    context != NULL;
		    context = AVL_NEXT(&caller->contexts.contexts, context)) {
			struct point_state *point_state;

			for (point_state = avl_first(&context->point_states);
			    point_state != NULL;
			    point_state = AVL_NEXT(&context->point_states,
			    point_state)) {
				struct function_info *callee;
				struct assumed_region *assumed;

				if (point_state->point.next_instruction == NULL ||
				    point_state->point.next_instruction->opcode !=
				    OP_CALL)
					continue;
				callee = callgraph_callee(caller,
				    point_state->point.next_instruction);
				if (callee == NULL)
					continue;
				for (assumed = callee->assumed_regions;
				    assumed != NULL; assumed = assumed->next) {
					struct assumed_call_finding *finding;

					if (!assumed->valid)
						continue;
					finding = assumed_call_finding(&tail,
					    findings,
					    point_state->point.next_instruction,
					    callee, assumed);
					check_assumed_call_state(analysis,
					    context,
					    point_state->point.next_instruction,
					    point_state->state, callee, assumed,
					    finding);
				}
			}
		}
	}
	callgraph_iter_close(iterator);
	while (findings != NULL) {
		struct assumed_call_finding *next = findings->next;
		const char *callee_name =
		    findings->callee->ep->name->ident != NULL ?
		    show_ident(findings->callee->ep->name->ident) :
		    "<anonymous>";

		if (findings->definite_failure && !findings->satisfied &&
		    !findings->conditional_failure) {
			if (findings->assumed->has_mutex) {
				locklint_warning(LOCKLINT_DIAG_UNPROTECTED_ACCESS,
				    findings->instruction->pos,
				    "call to '%s' does not satisfy assumed "
				    "protection for '%s': requires holding '%s'",
				    callee_name, findings->assumed->name,
				    findings->assumed->mutex_name);
			} else {
				locklint_warning(LOCKLINT_DIAG_UNPROTECTED_ACCESS,
				    findings->instruction->pos,
				    "call to '%s' does not satisfy assumed "
				    "protection for '%s'", callee_name,
				    findings->assumed->name);
			}
		} else if (findings->definite_failure ||
		    findings->conditional_failure) {
			locklint_warning(LOCKLINT_DIAG_CONDITIONAL_PROTECTION,
			    findings->instruction->pos,
			    "assumed protection for '%s' at call to '%s' is "
			    "not established on every path",
			    findings->assumed->name, callee_name);
		}
		free(findings);
		findings = next;
	}
}

static void
diagnose_invalid_assumed_regions(void)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct assumed_region *assumed;

		for (assumed = function->assumed_regions; assumed != NULL;
		    assumed = assumed->next) {
			if (!assumed->valid) {
				locklint_warning(
				    LOCKLINT_DIAG_INVALID_ASSUMING_PROTECTED,
				    assumed->marker->pos,
				    "ASSUMING_PROTECTED has no object");
			}
		}
	}
	callgraph_iter_close(iterator);
}

static int
compare_lock_identity_pointer(const void *left_arg, const void *right_arg)
{
	const struct lock_identity *const *left = left_arg;
	const struct lock_identity *const *right = right_arg;

	return (AVL_PCMP(*left, *right));
}

static bool
callee_automatic_lock(const struct function_context *context,
    const struct lock_identity *lock)
{
	return (lock->analysis_object_type == LOCK_ANALYSIS_OBJECT_SYMBOL &&
	    !binding_contains_analysis_object(context->bindings,
	    lock->key.analysis_object));
}

/*
 * Diagnose direct automatic locals which remain newly held at one return.
 * Caller-visible locks require declared-effect handling before they can be
 * checked here.
 */
static struct point_state *
diagnose_local_locks_at_return(struct function_context *context,
    struct point_state *first)
{
	const struct lock_identity **candidates;
	struct point_state *point_state;
	struct instruction *insn = first->point.next_instruction;
	size_t candidate_capacity = 0;
	size_t candidate_count = 0;
	size_t state_count = 0;
	size_t index;

	for (point_state = first; point_state != NULL &&
	    same_analysis_point(first, point_state);
	    point_state = AVL_NEXT(&context->point_states, point_state)) {
		size_t lock_count = point_state->state->locks->count;

		if (lock_count > SIZE_MAX - candidate_capacity)
			die("return lock candidate count overflow");
		candidate_capacity += lock_count;
		if (state_count == SIZE_MAX)
			die("return state count overflow");
		state_count++;
	}
	if (candidate_capacity > SIZE_MAX / sizeof (*candidates))
		die("return lock candidate allocation overflow");
	candidates = calloc(candidate_capacity, sizeof (*candidates));
	if (candidates == NULL && candidate_capacity != 0)
		die("cannot allocate return lock candidates");
	for (point_state = first; point_state != NULL &&
	    same_analysis_point(first, point_state);
	    point_state = AVL_NEXT(&context->point_states, point_state)) {
		const struct semantic_lock_set *locks =
		    point_state->state->locks;

		for (index = 0; index < locks->count; index++) {
			const struct lock_identity *lock =
			    locks->entries[index].lock;

			if (context_state_lock_modes(context->entry_state,
			    lock) == 0 &&
			    callee_automatic_lock(context, lock))
				candidates[candidate_count++] = lock;
		}
	}
	if (candidate_count > 1) {
		qsort(candidates, candidate_count, sizeof (*candidates),
		    compare_lock_identity_pointer);
	}
	for (index = 0; index < candidate_count; index++) {
		const struct lock_identity *lock = candidates[index];
		const struct symbol *symbol = lock->key.analysis_object;
		const char *name = symbol->ident != NULL ?
		    show_ident(symbol->ident) : "<unknown>";
		const char *function = context->function->ep->name->ident != NULL ?
		    show_ident(context->function->ep->name->ident) :
		    "<anonymous>";
		size_t held_count = 0;

		if (index != 0 && candidates[index - 1] == lock)
			continue;
		for (point_state = first; point_state != NULL &&
		    same_analysis_point(first, point_state);
		    point_state = AVL_NEXT(&context->point_states, point_state)) {
			if (context_state_lock_modes(point_state->state,
			    lock) != 0)
				held_count++;
		}
		if (held_count == state_count) {
			locklint_warning(LOCKLINT_DIAG_LOCK_HELD_ON_RETURN,
			    insn->pos, "lock '%s' held on return from '%s'",
			    name, function);
		} else {
			locklint_warning(LOCKLINT_DIAG_LOCK_MAYBE_HELD_ON_RETURN,
			    insn->pos, "lock '%s' held on only some paths "
			    "returning from '%s'", name, function);
		}
	}
	free(candidates);
	return (point_state);
}

static void
diagnose_local_locks_on_return(void)
{
	struct callgraph_iter *iterator;
	struct function_info *function;
	int error;

	error = callgraph_iter_open(&iterator);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	while ((function = callgraph_iter_next(iterator)) != NULL) {
		struct function_context *context;

		for (context = avl_first(&function->contexts.contexts);
		    context != NULL;
		    context = AVL_NEXT(&function->contexts.contexts, context)) {
			struct point_state *point_state =
			    avl_first(&context->point_states);

			while (point_state != NULL) {
				if (point_state->point.next_instruction == NULL ||
				    point_state->point.next_instruction->opcode !=
				    OP_RET) {
					point_state = AVL_NEXT(
					    &context->point_states, point_state);
					continue;
				}
				point_state = diagnose_local_locks_at_return(
				    context, point_state);
			}
		}
	}
	callgraph_iter_close(iterator);
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
		struct binding_environment *bindings;
		struct function_context *context;
		struct semantic_lock_set *locks;
		struct semantic_visibility_set *visibility;
		struct semantic_state *state;
		size_t contexts = context_count(function);
		size_t binding_environments =
		    binding_environment_count(&function->bindings);
		size_t states = context_state_count(function);
		size_t visibility_sets =
		    context_visibility_set_count(function);

		distribution_add(&measurements->contexts_per_function, contexts,
		    function);
		distribution_add(
		    &measurements->binding_environments_per_function,
		    binding_environments, function);
		for (bindings = avl_first(&function->bindings.environments);
		    bindings != NULL;
		    bindings = AVL_NEXT(&function->bindings.environments,
		    bindings)) {
			distribution_add(
			    &measurements->bindings_per_environment,
			    bindings->count, function);
			memory_add(
			    &measurements->binding_environment_bytes, 1,
			    sizeof (*bindings) +
			    bindings->count * sizeof (*bindings->entries));
		}
		distribution_add(&measurements->semantic_states_per_function,
		    states, function);
		distribution_add(&measurements->visibility_sets_per_function,
		    visibility_sets, function);
		memory_add(&measurements->visibility_sets_created,
		    context_visibility_sets_created(function), 1);
		memory_add(&measurements->visibility_sets_reused,
		    context_visibility_sets_reused(function), 1);
		for (state = avl_first(&function->contexts.semantic_states);
		    state != NULL;
		    state = AVL_NEXT(&function->contexts.semantic_states, state)) {
			distribution_add(
			    &measurements->locks_per_semantic_state,
			    context_state_lock_count(state), function);
			distribution_add(
			    &measurements->visibility_per_semantic_state,
			    context_state_visibility_count(state), function);
		}
		for (locks = avl_first(&function->contexts.lock_sets);
		    locks != NULL;
		    locks = AVL_NEXT(&function->contexts.lock_sets, locks)) {
			memory_add(&measurements->lock_set_bytes, 1,
			    sizeof (*locks) +
			    locks->count * sizeof (*locks->entries));
		}
		for (visibility =
		    avl_first(&function->contexts.visibility_sets);
		    visibility != NULL;
		    visibility = AVL_NEXT(&function->contexts.visibility_sets,
		    visibility)) {
			distribution_add(
			    &measurements->visibility_entries_per_set,
			    visibility->count, function);
			memory_add(&measurements->visibility_set_bytes, 1,
			    sizeof (*visibility) +
			    visibility->count *
			    sizeof (*visibility->entries));
		}
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
		measurements->lock_identity_bytes,
		measurements->binding_environment_bytes,
		measurements->lock_set_bytes,
		measurements->visibility_set_bytes,
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
measure_lock_identities(struct analysis *analysis)
{
	struct analysis_measurements *measurements = &analysis->measurements;
	struct lock_identity *identity;
	const void *previous_analysis_object = NULL;

	for (identity = lock_identity_first(analysis->lock_identities);
	    identity != NULL;
	    identity = lock_identity_next(analysis->lock_identities, identity)) {
		if (identity->key.analysis_object != previous_analysis_object) {
			measurements->lock_identity_analysis_objects++;
			previous_analysis_object = identity->key.analysis_object;
		}
		if (identity->analysis_object_type <
		    LOCK_ANALYSIS_OBJECT_UNSPECIFIED ||
		    identity->analysis_object_type >
		    LOCK_ANALYSIS_OBJECT_PSEUDO) {
			die("invalid lock analysis-object type");
		}
		measurements->lock_identity_types[
		    identity->analysis_object_type]++;
	}
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
	(void) fprintf(stream,
	    "visibility-sets created %zu reused %zu retained %zu\n",
	    measurements->visibility_sets_created,
	    measurements->visibility_sets_reused,
	    measurements->visibility_entries_per_set.samples);
	(void) fprintf(stream, "binding-environments created %zu reused %zu\n",
	    counts->binding_environments_created,
	    counts->binding_environments_reused);
	(void) fprintf(stream, "binding-identities composed %zu\n",
	    counts->binding_identities_composed);
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
	(void) fprintf(stream,
	    "lock-identities created %zu reused %zu unresolved %zu "
	    "retained %zu\n", counts->lock_identities_created,
	    counts->lock_identities_reused,
	    counts->lock_identities_unresolved,
	    measurements->lock_identities);
	(void) fprintf(stream, "lock-transitions applied %zu deferred %zu\n",
	    counts->lock_transitions_applied,
	    counts->lock_transitions_deferred);
	(void) fprintf(stream,
	    "visibility-transitions applied %zu deferred %zu unresolved %zu\n",
	    counts->visibility_transitions_applied,
	    counts->visibility_transitions_deferred,
	    counts->visibility_transitions_unresolved);
	(void) fprintf(stream,
	    "competition-transitions applied %zu backedges-widened %zu "
	    "backedges-covered %zu\n",
	    counts->competition_transitions_applied,
	    counts->competition_backedges_widened,
	    counts->competition_backedges_covered);
	(void) fprintf(stream, "return-states mapped %zu locks-filtered %zu\n",
	    counts->return_states_mapped, counts->return_locks_filtered);
	(void) fprintf(stream,
	    "lock-identity-types unspecified %zu object %zu symbol %zu "
	    "pseudo %zu\n",
	    measurements->lock_identity_types[
	    LOCK_ANALYSIS_OBJECT_UNSPECIFIED],
	    measurements->lock_identity_types[
	    LOCK_ANALYSIS_OBJECT_OBJECT_IDENTITY],
	    measurements->lock_identity_types[LOCK_ANALYSIS_OBJECT_SYMBOL],
	    measurements->lock_identity_types[LOCK_ANALYSIS_OBJECT_PSEUDO]);
	(void) fprintf(stream, "lock-identity-analysis-objects %zu\n",
	    measurements->lock_identity_analysis_objects);
	show_distribution(stream, "contexts/function",
	    &measurements->contexts_per_function);
	show_distribution(stream, "binding-environments/function",
	    &measurements->binding_environments_per_function);
	show_distribution(stream, "bindings/environment",
	    &measurements->bindings_per_environment);
	show_distribution(stream, "semantic-states/function",
	    &measurements->semantic_states_per_function);
	show_distribution(stream, "visibility-sets/function",
	    &measurements->visibility_sets_per_function);
	show_distribution(stream, "visibility-entries/set",
	    &measurements->visibility_entries_per_set);
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
	show_maximum_owner(stream, "binding-environments/function",
	    &measurements->binding_environments_per_function);
	show_maximum_owner(stream, "bindings/environment",
	    &measurements->bindings_per_environment);
	show_maximum_owner(stream, "semantic-states/function",
	    &measurements->semantic_states_per_function);
	show_maximum_owner(stream, "visibility-sets/function",
	    &measurements->visibility_sets_per_function);
	show_maximum_owner(stream, "visibility-entries/set",
	    &measurements->visibility_entries_per_set);
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
	(void) fprintf(stream, "memory lock-identities %zu bytes\n",
	    measurements->lock_identity_bytes);
	(void) fprintf(stream, "memory binding-environments %zu bytes\n",
	    measurements->binding_environment_bytes);
	(void) fprintf(stream, "memory lock-sets %zu bytes\n",
	    measurements->lock_set_bytes);
	(void) fprintf(stream, "memory visibility-sets %zu bytes\n",
	    measurements->visibility_set_bytes);
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
analysis_run(struct lock_identity_collection *lock_identities, FILE *stream)
{
	struct analysis analysis = {
		.lock_identities = lock_identities
	};
	struct point_state *point_state;

	worklist_create(&analysis.worklist);
	collect_assumed_regions();
	seed_roots(&analysis);
	while ((point_state =
	    worklist_point_state_dequeue(&analysis.worklist)) != NULL)
		process_point(&analysis, point_state);
	diagnose_lock_transitions(&analysis);
	diagnose_competition_underflow();
	diagnose_declared_competition_effects();
	diagnose_competition_assertions();
	diagnose_protected_accesses(&analysis);
	diagnose_assumed_calls(&analysis);
	diagnose_invalid_assumed_regions();
	diagnose_local_locks_on_return();
	if (stream != NULL) {
		analysis.measurements.lock_identities =
		    lock_identity_count(analysis.lock_identities);
		measure_lock_identities(&analysis);
		memory_add(&analysis.measurements.lock_identity_bytes,
		    analysis.measurements.lock_identities,
		    sizeof (struct lock_identity));
		measure_collections(&analysis);
		show_counts(stream, &analysis);
	}
}
