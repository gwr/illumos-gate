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
 * Perform flow-sensitive and interprocedural lock-state analysis.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "annotations.h"
#include "assertions.h"
#include "callgraph.h"
#include "check.h"
#include "function_info.h"
#include "events.h"
#include "identity.h"
#include "lock_order.h"
#include "symbol.h"

typedef unsigned int lock_state_t;

#define	LOCK_NOT_HELD	LOCKLINT_MODE_UNHELD
#define	LOCK_HELD	LOCKLINT_MODE_MUTEX
#define	LOCK_READ_HELD	LOCKLINT_MODE_READER
#define	LOCK_WRITE_HELD	LOCKLINT_MODE_WRITER
#define	LOCK_STATE_COUNT	(1 << 4)

#define	LOCK_ANY_HELD	(LOCK_HELD | LOCK_READ_HELD | LOCK_WRITE_HELD)
#define	ALL_LOCK_INPUTS	((1U << LOCK_STATE_COUNT) - 2)

enum visibility_state {
	VISIBILITY_INVISIBLE,
	VISIBILITY_MAYBE,
	VISIBILITY_VISIBLE,
	VISIBILITY_STATE_COUNT
};

enum protection_status {
	PROTECTION_ABSENT,
	PROTECTION_PATH_DEPENDENT,
	PROTECTION_DEFINITE
};

#define	INVALID_ACQUIRE	0x1
#define	INVALID_RELEASE	0x2

struct state_entry {
	struct locklint_access lock;
	lock_state_t state;
	bool side_effect;
	bool has_acquire_pos;
	struct position acquire_pos;
	struct state_entry *next;
};

struct visibility_entry {
	struct locklint_access region;
	enum visibility_state state;
	struct visibility_entry *next;
};

struct assumed_region {
	struct locklint_access region;
	struct position pos;
	struct assumed_region *next;
};

struct competition_state {
	int minimum;
	int maximum;
	bool minimum_unbounded;
	bool maximum_unbounded;
	bool path_dependent;
	bool ambient;
};

struct analysis_state {
	struct state_entry *locks;
	struct competition_state competition;
	struct visibility_entry *visibility;
};

struct block_info {
	struct basic_block *bb;
	bool reachable;
	bool input_complete;
	struct analysis_state *in;
	struct analysis_state *out;
	struct block_info *next;
};

struct competition_transfer {
	struct competition_state output;
	struct competition_state ambient_output;
	int minimum_prefix;
	bool minimum_prefix_unbounded;
	int ambient_minimum_prefix;
	bool ambient_minimum_prefix_unbounded;
};

struct competition_transfer_block_info {
	struct basic_block *bb;
	bool reachable;
	bool input_complete;
	struct competition_state in;
	struct competition_state out;
	int minimum_prefix_in;
	int minimum_prefix_out;
	bool minimum_prefix_in_unbounded;
	bool minimum_prefix_out_unbounded;
	struct competition_transfer_block_info *next;
};

struct transfer_block_info {
	struct basic_block *bb;
	bool reachable;
	lock_state_t in;
	lock_state_t out;
	unsigned int invalid_in;
	unsigned int invalid_out;
	struct transfer_block_info *next;
};

struct visibility_transfer_block_info {
	struct basic_block *bb;
	bool reachable;
	enum visibility_state in;
	enum visibility_state out;
	struct visibility_transfer_block_info *next;
};

struct protection_alternative {
	unsigned int argument;
	struct symbol *lock_member;
	unsigned long lock_offset;
	struct protection_alternative *next;
};

struct protection_condition {
	unsigned int argument;
	/*
	 * A NULL data root is relative to argument.  An absolute datum retains
	 * both its source symbol and canonical identity.
	 */
	struct symbol *data_root;
	struct object_identity *data_object;
	/*
	 * A NULL root is relative to argument.  An absolute root retains both
	 * its source symbol and canonical identity across translation units.
	 */
	struct symbol *lock_root;
	struct object_identity *lock_object;
	struct symbol *lock_member;
	unsigned long lock_offset;
	bool has_lock;
	unsigned int required_modes;
	struct symbol *data_member;
	unsigned long data_offset;
	struct position pos;
	struct protection_alternative *alternatives;
	struct protection_condition *next;
};

struct acquisition_role {
	struct locklint_access access;
	unsigned int argument;
	bool relative;
};

struct assertion_alternative_role {
	struct acquisition_role role;
	struct assertion_alternative_role *next;
};

struct assertion_alternative {
	struct assertion_alternative_role *roles;
	unsigned int accepted_inputs;
	struct assertion_alternative *next;
};

struct assertion_requirement {
	struct acquisition_role role;
	unsigned int modes;
	unsigned int accepted_inputs;
	struct assertion_alternative *alternatives;
	struct function_info *source_function;
	struct instruction *checkpoint;
	struct position pos;
	struct translation_unit *origin_tu;
	struct assertion_requirement *next;
};

struct lock_transfer {
	struct acquisition_role role;
	lock_state_t output[LOCK_STATE_COUNT];
	unsigned int invalid[LOCK_STATE_COUNT];
	struct lock_transfer *next;
};

struct mapped_transfer_role {
	struct lock_transfer *transfer;
	struct mapped_transfer_role *next;
};

struct mapped_lock_transfer {
	struct locklint_access lock;
	unsigned int role_count;
	struct mapped_transfer_role *roles;
	struct mapped_lock_transfer *next;
};

struct alias_replay_frame {
	struct function_info *function;
	const struct mapped_transfer_role *roles;
	lock_state_t input;
	lock_state_t output;
	unsigned int invalid;
	bool iterative;
	const struct alias_replay_frame *next;
};

struct transfer_target {
	const struct locklint_access *single;
	const struct mapped_transfer_role *roles;
	const struct alias_replay_frame *replay;
};

struct acquisition_prefix {
	struct acquisition_role role;
	lock_state_t output[LOCK_STATE_COUNT];
	bool unknown;
	struct acquisition_prefix *next;
};

struct acquisition_summary {
	struct acquisition_role acquired;
	struct position pos;
	struct acquisition_prefix *prefixes;
	struct function_info *source_function;
	struct instruction *checkpoint;
	struct acquisition_summary *next;
};

struct acquisition_candidate {
	struct acquisition_role role;
	struct acquisition_candidate *next;
};

struct visibility_transfer {
	unsigned int argument;
	struct symbol *data_root;
	struct object_identity *data_object;
	struct symbol *data_member;
	unsigned long data_offset;
	struct locklint_member_path *data_path;
	enum visibility_state output[VISIBILITY_STATE_COUNT];
	struct visibility_transfer *next;
};

struct mapped_visibility_effect {
	struct locklint_access region;
	enum visibility_state output[VISIBILITY_STATE_COUNT];
	struct mapped_visibility_effect *next;
};

static void transfer_call_effects(struct function_info *,
    struct state_entry **, struct instruction *);
static void transfer_call_visibility_effects(struct function_info *,
    struct visibility_entry **, struct instruction *);
static lock_state_t simulate_aliased_transfer(struct function_info *,
    const struct mapped_transfer_role *, lock_state_t, unsigned int *);
static lock_state_t simulate_aliased_transfer_context(struct function_info *,
    const struct mapped_transfer_role *, lock_state_t, unsigned int *,
    const struct alias_replay_frame *);
static struct symbol *formal_symbol(struct entrypoint *, unsigned int);
static bool acquisition_block_reachable(struct function_info *,
    struct basic_block *);
static bool function_declares_lock_effect(struct function_info *,
    const struct locklint_access *);

static bool
same_lock(const struct locklint_access *left,
    const struct locklint_access *right)
{
	return (locklint_same_access(left, right));
}

static struct state_entry *
alloc_state(const struct locklint_access *lock, lock_state_t state)
{
	struct state_entry *entry;

	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory analyzing lock state");
	entry->lock = *lock;
	entry->state = state;
	entry->side_effect = true;
	return (entry);
}

static void
free_states(struct state_entry *states)
{
	while (states != NULL) {
		struct state_entry *next = states->next;

		free(states);
		states = next;
	}
}

static lock_state_t
get_state(struct state_entry *states, const struct locklint_access *lock)
{
	struct state_entry *entry;

	for (entry = states; entry != NULL; entry = entry->next) {
		if (same_lock(&entry->lock, lock))
			return (entry->state);
	}
	return (LOCK_NOT_HELD);
}

static bool
state_definitely_held(lock_state_t state)
{
	return ((state & LOCK_ANY_HELD) != 0 &&
	    (state & LOCK_NOT_HELD) == 0);
}

static bool
state_maybe_held(lock_state_t state)
{
	return ((state & LOCK_ANY_HELD) != 0 &&
	    (state & LOCK_NOT_HELD) != 0);
}

static bool
state_satisfies_modes(lock_state_t state, unsigned int required_modes)
{
	return ((state & required_modes) != 0 &&
	    (state & ~required_modes) == 0);
}

static bool
has_side_effect(struct state_entry *states,
    const struct locklint_access *lock)
{
	struct state_entry *entry;

	for (entry = states; entry != NULL; entry = entry->next) {
		if (same_lock(&entry->lock, lock))
			return (entry->side_effect);
	}
	return (false);
}

static void
set_state(struct state_entry **states, const struct locklint_access *lock,
    lock_state_t state)
{
	struct state_entry **link;

	for (link = states; *link != NULL; link = &(*link)->next) {
		if (!same_lock(&(*link)->lock, lock))
			continue;
		if (state == LOCK_NOT_HELD) {
			struct state_entry *old = *link;

			*link = old->next;
			free(old);
		} else {
			if ((*link)->state != state)
				(*link)->side_effect = true;
			(*link)->state = state;
		}
		return;
	}
	if (state != LOCK_NOT_HELD) {
		struct state_entry *entry = alloc_state(lock, state);

		entry->next = *states;
		*states = entry;
	}
}

static void
set_acquire_position(struct state_entry *states,
    const struct locklint_access *lock, struct position pos)
{
	struct state_entry *entry;

	for (entry = states; entry != NULL; entry = entry->next) {
		if (!same_lock(&entry->lock, lock))
			continue;
		entry->has_acquire_pos = true;
		entry->acquire_pos = pos;
		return;
	}
}

static void
set_asserted_state(struct state_entry **states,
    const struct locklint_access *lock, lock_state_t state)
{
	struct state_entry *entry;

	for (entry = *states; entry != NULL; entry = entry->next) {
		if (!same_lock(&entry->lock, lock))
			continue;
		if (state == LOCK_NOT_HELD) {
			set_state(states, lock, state);
		} else {
			entry->state = state;
		}
		return;
	}
	if (state != LOCK_NOT_HELD) {
		set_state(states, lock, state);
		(*states)->side_effect = false;
	}
}

static struct state_entry *
copy_states(struct state_entry *states)
{
	struct state_entry *copy = NULL;
	struct state_entry **tail = &copy;
	struct state_entry *entry;

	for (entry = states; entry != NULL; entry = entry->next) {
		*tail = alloc_state(&entry->lock, entry->state);
		(*tail)->side_effect = entry->side_effect;
		(*tail)->has_acquire_pos = entry->has_acquire_pos;
		(*tail)->acquire_pos = entry->acquire_pos;
		tail = &(*tail)->next;
	}
	return (copy);
}

static bool
same_states(struct state_entry *left, struct state_entry *right)
{
	struct state_entry *entry;

	for (entry = left; entry != NULL; entry = entry->next) {
		if (get_state(right, &entry->lock) != entry->state ||
		    has_side_effect(right, &entry->lock) != entry->side_effect)
			return (false);
	}
	for (entry = right; entry != NULL; entry = entry->next) {
		if (get_state(left, &entry->lock) != entry->state ||
		    has_side_effect(left, &entry->lock) != entry->side_effect)
			return (false);
	}
	return (true);
}

static void
merge_states(struct state_entry **merged, struct state_entry *incoming)
{
	struct state_entry *entry;

	for (entry = *merged; entry != NULL; entry = entry->next) {
		struct state_entry *other;

		for (other = incoming; other != NULL; other = other->next) {
			if (same_lock(&entry->lock, &other->lock)) {
				entry->side_effect |= other->side_effect;
				if (!entry->has_acquire_pos &&
				    other->has_acquire_pos) {
					entry->has_acquire_pos = true;
					entry->acquire_pos = other->acquire_pos;
				}
				break;
			}
		}
		entry->state |= get_state(incoming, &entry->lock);
	}
	for (entry = incoming; entry != NULL; entry = entry->next) {
		if (get_state(*merged, &entry->lock) == LOCK_NOT_HELD) {
			set_state(merged, &entry->lock,
			    entry->state | LOCK_NOT_HELD);
			(*merged)->side_effect = entry->side_effect;
			(*merged)->has_acquire_pos = entry->has_acquire_pos;
			(*merged)->acquire_pos = entry->acquire_pos;
		}
	}
}

static struct visibility_entry *
alloc_visibility(const struct locklint_access *region,
    enum visibility_state state)
{
	struct visibility_entry *entry;

	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory analyzing object visibility");
	entry->region = *region;
	entry->state = state;
	return (entry);
}

static void
free_visibility(struct visibility_entry *entries)
{
	while (entries != NULL) {
		struct visibility_entry *next = entries->next;

		free(entries);
		entries = next;
	}
}

static struct visibility_entry *
copy_visibility(struct visibility_entry *entries)
{
	struct visibility_entry *copy = NULL;
	struct visibility_entry **tail = &copy;

	for (; entries != NULL; entries = entries->next) {
		*tail = alloc_visibility(&entries->region, entries->state);
		tail = &(*tail)->next;
	}
	return (copy);
}

static enum visibility_state
get_visibility(struct visibility_entry *entries,
    const struct locklint_access *region)
{
	struct visibility_entry *best = NULL;
	struct visibility_entry *entry;

	/* A narrower covering fact overrides a fact about its containing object. */
	for (entry = entries; entry != NULL; entry = entry->next) {
		if (!locklint_access_contains(&entry->region, region))
			continue;
		if (best == NULL ||
		    locklint_access_contains(&best->region, &entry->region))
			best = entry;
	}
	return (best != NULL ? best->state : VISIBILITY_VISIBLE);
}

static void
set_visibility(struct visibility_entry **entries,
    const struct locklint_access *region, enum visibility_state state)
{
	struct visibility_entry **link;
	struct visibility_entry *entry;

	/* A transition for a region replaces all facts wholly within it. */
	for (link = entries; *link != NULL; ) {
		if (locklint_access_contains(region, &(*link)->region)) {
			struct visibility_entry *old = *link;

			*link = old->next;
			free(old);
		} else {
			link = &(*link)->next;
		}
	}
	entry = alloc_visibility(region, state);
	entry->next = *entries;
	*entries = entry;
}

static bool
same_visibility(struct visibility_entry *left,
    struct visibility_entry *right)
{
	struct visibility_entry *entry;

	for (entry = left; entry != NULL; entry = entry->next) {
		if (get_visibility(right, &entry->region) != entry->state)
			return (false);
	}
	for (entry = right; entry != NULL; entry = entry->next) {
		if (get_visibility(left, &entry->region) != entry->state)
			return (false);
	}
	return (true);
}

static bool
has_visibility_region(struct visibility_entry *entries,
    const struct locklint_access *region)
{
	for (; entries != NULL; entries = entries->next) {
		if (locklint_same_access(&entries->region, region))
			return (true);
	}
	return (false);
}

static enum visibility_state
merge_visibility_state(enum visibility_state left,
    enum visibility_state right)
{
	return (left == right ? left : VISIBILITY_MAYBE);
}

static void
merge_visibility(struct visibility_entry **merged,
    struct visibility_entry *incoming)
{
	struct visibility_entry *result = NULL;
	struct visibility_entry **tail = &result;
	struct visibility_entry *entry;

	/*
	 * Retain every region distinguished on either path and merge its
	 * effective state, including state inherited from a containing region.
	 */
	for (entry = *merged; entry != NULL; entry = entry->next) {
		*tail = alloc_visibility(&entry->region,
		    merge_visibility_state(entry->state,
		    get_visibility(incoming, &entry->region)));
		tail = &(*tail)->next;
	}
	for (entry = incoming; entry != NULL; entry = entry->next) {
		if (has_visibility_region(result, &entry->region))
			continue;
		*tail = alloc_visibility(&entry->region,
		    merge_visibility_state(get_visibility(*merged,
		    &entry->region), entry->state));
		tail = &(*tail)->next;
	}
	free_visibility(*merged);
	*merged = result;
}

static struct analysis_state *
copy_analysis_state(const struct analysis_state *state)
{
	struct analysis_state *copy;

	copy = calloc(1, sizeof (*copy));
	if (copy == NULL)
		die("out of memory copying analysis state");
	if (state != NULL) {
		copy->locks = copy_states(state->locks);
		copy->competition = state->competition;
		copy->visibility = copy_visibility(state->visibility);
	} else {
		copy->competition.minimum = 0;
		copy->competition.maximum = 1;
		copy->competition.ambient = true;
	}
	return (copy);
}

static void
free_analysis_state(struct analysis_state *state)
{
	if (state == NULL)
		return;
	free_states(state->locks);
	free_visibility(state->visibility);
	free(state);
}

static bool
same_competition_state(const struct competition_state *left,
    const struct competition_state *right)
{
	return (left->minimum_unbounded == right->minimum_unbounded &&
	    left->maximum_unbounded == right->maximum_unbounded &&
	    (left->minimum_unbounded ||
	    left->minimum == right->minimum) &&
	    (left->maximum_unbounded ||
	    left->maximum == right->maximum) &&
	    left->path_dependent == right->path_dependent &&
	    left->ambient == right->ambient);
}

static bool
competition_absent(const struct competition_state *state)
{
	return (!state->maximum_unbounded && state->maximum <= 0);
}

static bool
competition_present(const struct competition_state *state)
{
	return (!state->minimum_unbounded && state->minimum > 0);
}

static bool
same_analysis_state(const struct analysis_state *left,
    const struct analysis_state *right)
{
	const struct competition_state initial_competition = {
		.minimum = 0,
		.maximum = 1,
		.ambient = true
	};

	return (same_states(left != NULL ? left->locks : NULL,
	    right != NULL ? right->locks : NULL) &&
	    same_competition_state(left != NULL ? &left->competition :
	    &initial_competition, right != NULL ? &right->competition :
	    &initial_competition) &&
	    same_visibility(left != NULL ? left->visibility : NULL,
	    right != NULL ? right->visibility : NULL));
}

static void
merge_competition_state(struct competition_state *merged,
    const struct competition_state *incoming)
{
	if (!same_competition_state(merged, incoming))
		merged->path_dependent = true;
	if (incoming->minimum_unbounded) {
		merged->minimum_unbounded = true;
		merged->minimum = 0;
	} else if (!merged->minimum_unbounded &&
	    incoming->minimum < merged->minimum) {
		merged->minimum = incoming->minimum;
	}
	if (incoming->maximum_unbounded) {
		merged->maximum_unbounded = true;
		merged->maximum = 0;
	} else if (!merged->maximum_unbounded &&
	    incoming->maximum > merged->maximum) {
		merged->maximum = incoming->maximum;
	}
	merged->ambient &= incoming->ambient;
}

static void
widen_competition_state(struct competition_state *state,
    const struct competition_state *previous)
{
	if (previous->minimum_unbounded ||
	    (!state->minimum_unbounded && state->minimum < previous->minimum)) {
		state->minimum_unbounded = true;
		state->minimum = 0;
		state->path_dependent = true;
		state->ambient = false;
	}
	if (previous->maximum_unbounded ||
	    (!state->maximum_unbounded && state->maximum > previous->maximum)) {
		state->maximum_unbounded = true;
		state->maximum = 0;
		state->path_dependent = true;
		state->ambient = false;
	}
}

static void
merge_analysis_state(struct analysis_state *merged,
    const struct analysis_state *incoming)
{
	merge_states(&merged->locks, incoming->locks);
	merge_competition_state(&merged->competition, &incoming->competition);
	merge_visibility(&merged->visibility, incoming->visibility);
}

static struct block_info *
find_block(struct block_info *blocks, struct basic_block *bb)
{
	struct block_info *block;

	for (block = blocks; block != NULL; block = block->next) {
		if (block->bb == bb)
			return (block);
	}
	return (NULL);
}

static void
transfer_lock_action(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	struct locklint_access lock;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;

	action = locklint_get_lock_action(function->tu, insn, &lock, &mode);
	if (action == LOCKLINT_LOCK_ACQUIRE && lock.root != NULL) {
		set_state(states, &lock, mode);
		set_acquire_position(*states, &lock,
		    insn->call_expr != NULL ? insn->call_expr->pos : insn->pos);
	} else if (action == LOCKLINT_LOCK_RELEASE && lock.root != NULL) {
		set_state(states, &lock, LOCK_NOT_HELD);
	}
}

static void
transfer_assertion(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	struct locklint_access lock;
	unsigned int modes;

	if (locklint_get_assertion(function->tu, insn, &lock, &modes))
		set_asserted_state(states, &lock, modes);
}

static void
shift_competition_state(struct competition_state *state, int change)
{
	if (!state->minimum_unbounded)
		state->minimum += change;
	if (!state->maximum_unbounded)
		state->maximum += change;
}

static void
change_competition_state(struct competition_state *state, int change)
{
	if (state->ambient) {
		state->minimum = change > 0 ? change : change + 1;
		state->maximum = state->minimum;
		state->ambient = false;
	} else {
		shift_competition_state(state, change);
	}
}

static void
apply_competition_transfer(struct competition_state *state,
    const struct competition_transfer *transfer)
{
	struct competition_state input;

	if (state->ambient) {
		*state = transfer->ambient_output;
		return;
	}
	input = *state;
	state->minimum_unbounded |= transfer->output.minimum_unbounded;
	state->maximum_unbounded |= transfer->output.maximum_unbounded;
	if (state->minimum_unbounded) {
		state->minimum = 0;
	} else {
		state->minimum = input.minimum + transfer->output.minimum;
	}
	if (state->maximum_unbounded) {
		state->maximum = 0;
	} else {
		state->maximum = input.maximum + transfer->output.maximum;
	}
	state->path_dependent |= transfer->output.path_dependent ||
	    transfer->output.minimum_unbounded ||
	    transfer->output.maximum_unbounded ||
	    transfer->output.minimum != transfer->output.maximum;
	state->ambient = false;
}

static void
transfer_call_competition_effect(struct function_info *function,
    struct competition_state *state, const struct instruction *insn)
{
	struct function_info *callee = callgraph_callee(function, insn);

	if (callee != NULL && callee->competition_transfer != NULL)
		apply_competition_transfer(state, callee->competition_transfer);
}

static void
transfer_competition_state(struct analysis_state *state,
    const struct instruction *insn)
{
	switch (locklint_get_execution_annotation(insn)) {
	case LOCKLINT_EXECUTION_ASSERT_NO_COMPETITION:
		state->competition.minimum = 0;
		state->competition.maximum = 0;
		state->competition.minimum_unbounded = false;
		state->competition.maximum_unbounded = false;
		state->competition.path_dependent = false;
		state->competition.ambient = false;
		break;
	case LOCKLINT_EXECUTION_NO_COMPETITION:
		change_competition_state(&state->competition, -1);
		break;
	case LOCKLINT_EXECUTION_COMPETITION:
		change_competition_state(&state->competition, 1);
		break;
	default:
		break;
	}
}

static void
transfer_visibility_expression(struct function_info *function,
    struct analysis_state *state, struct expression *expr,
    enum visibility_state visibility, bool diagnose)
{
	struct locklint_access region;

	if (expr == NULL)
		return;
	if (expr->type == EXPR_COMMA) {
		transfer_visibility_expression(function, state, expr->left,
		    visibility, diagnose);
		transfer_visibility_expression(function, state, expr->right,
		    visibility, diagnose);
		return;
	}
	if (!locklint_get_access(function->tu, expr, &region)) {
		if (diagnose) {
			warning(expr->pos,
			    "locklint: visibility annotation has no object");
		}
		return;
	}
	set_visibility(&state->visibility, &region, visibility);
}

static void
transfer_visibility_state(struct function_info *function,
    struct analysis_state *state, const struct instruction *insn,
    bool diagnose)
{
	enum visibility_state visibility;

	switch (locklint_get_execution_annotation(insn)) {
	case LOCKLINT_EXECUTION_INVISIBLE:
		visibility = VISIBILITY_INVISIBLE;
		break;
	case LOCKLINT_EXECUTION_VISIBLE:
		visibility = VISIBILITY_VISIBLE;
		break;
	default:
		return;
	}
	transfer_visibility_expression(function, state, insn->context_expr,
	    visibility, diagnose);
}

static void
transfer_instruction(struct function_info *function,
    struct analysis_state *state, struct instruction *insn)
{
	transfer_call_effects(function, &state->locks, insn);
	transfer_call_visibility_effects(function, &state->visibility, insn);
	transfer_call_competition_effect(function, &state->competition, insn);
	transfer_lock_action(function, &state->locks, insn);
	transfer_assertion(function, &state->locks, insn);
	transfer_competition_state(state, insn);
	transfer_visibility_state(function, state, insn, false);
}

static struct analysis_state *
transfer_block(struct function_info *function, struct basic_block *bb,
    const struct analysis_state *in)
{
	struct analysis_state *out = copy_analysis_state(in);
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (insn->bb == NULL)
			continue;
		transfer_instruction(function, out, insn);
	} END_FOR_EACH_PTR(insn);

	return (out);
}

static struct analysis_state *
merge_parents(struct block_info *blocks, struct basic_block *bb,
    bool *reachable, bool *complete)
{
	struct analysis_state *merged = NULL;
	struct basic_block *parent;
	bool first = true;

	*reachable = false;
	*complete = true;
	FOR_EACH_PTR(bb->parents, parent) {
		struct block_info *block = find_block(blocks, parent);

		if (block == NULL || !block->reachable) {
			*complete = false;
			continue;
		}
		if (first) {
			merged = copy_analysis_state(block->out);
			first = false;
		} else {
			merge_analysis_state(merged, block->out);
		}
		*reachable = true;
	} END_FOR_EACH_PTR(parent);
	return (merged);
}

/*
 * Solve intraprocedural lock state to a fixed point and return the stable
 * input and output state retained for every basic block.
 */
static struct block_info *
analyze_blocks(struct function_info *function)
{
	struct entrypoint *ep = function->ep;
	struct block_info *blocks = NULL;
	struct block_info **tail = &blocks;
	struct basic_block *bb;
	bool changed;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct block_info *block = calloc(1, sizeof (*block));

		if (block == NULL)
			die("out of memory analyzing control flow");
		block->bb = bb;
		*tail = block;
		tail = &block->next;
	} END_FOR_EACH_PTR(bb);

	do {
		struct block_info *block;

		changed = false;
		for (block = blocks; block != NULL; block = block->next) {
			struct analysis_state *in;
			struct analysis_state *out;
			bool complete;
			bool reachable;

			if (block->bb == ep->entry->bb) {
				in = copy_analysis_state(NULL);
				complete = true;
				reachable = true;
			} else {
				in = merge_parents(blocks, block->bb, &reachable,
				    &complete);
			}
			if (!reachable) {
				free_analysis_state(in);
				continue;
			}
			if (block->input_complete && complete) {
				widen_competition_state(&in->competition,
				    &block->in->competition);
			}
			out = transfer_block(function, block->bb, in);
			if (!block->reachable ||
			    !same_analysis_state(block->in, in) ||
			    !same_analysis_state(block->out, out)) {
				changed = true;
			}
			free_analysis_state(block->in);
			free_analysis_state(block->out);
			block->in = in;
			block->out = out;
			block->reachable = true;
			block->input_complete = complete;
		}
	} while (changed);

	return (blocks);
}

static struct competition_transfer_block_info *
find_competition_transfer_block(struct competition_transfer_block_info *blocks,
    struct basic_block *bb)
{
	struct competition_transfer_block_info *block;

	for (block = blocks; block != NULL; block = block->next) {
		if (block->bb == bb)
			return (block);
	}
	return (NULL);
}

static void
update_competition_minimum_prefix(const struct competition_state *state,
    int *minimum, bool *unbounded)
{
	if (*unbounded)
		return;
	if (state->minimum_unbounded) {
		*minimum = 0;
		*unbounded = true;
	} else if (state->minimum < *minimum) {
		*minimum = state->minimum;
	}
}

static void
simulate_competition_transfer_instruction(struct function_info *function,
    struct competition_state *state, int *minimum_prefix,
    bool *minimum_prefix_unbounded, struct instruction *insn)
{
	struct function_info *callee = callgraph_callee(function, insn);
	enum locklint_execution_kind kind;

	if (callee != NULL && callee->competition_transfer != NULL) {
		struct competition_transfer *transfer =
		    callee->competition_transfer;
		int callee_minimum;
		bool callee_minimum_unbounded;

		if (state->ambient) {
			callee_minimum = transfer->ambient_minimum_prefix;
			callee_minimum_unbounded =
			    transfer->ambient_minimum_prefix_unbounded;
		} else {
			callee_minimum = state->minimum +
			    transfer->minimum_prefix;
			callee_minimum_unbounded =
			    transfer->minimum_prefix_unbounded ||
			    state->minimum_unbounded;
		}
		if (callee_minimum_unbounded ||
		    state->minimum_unbounded) {
			*minimum_prefix = 0;
			*minimum_prefix_unbounded = true;
		} else if (!*minimum_prefix_unbounded &&
		    callee_minimum < *minimum_prefix) {
			*minimum_prefix = callee_minimum;
		}
		apply_competition_transfer(state, transfer);
		update_competition_minimum_prefix(state, minimum_prefix,
		    minimum_prefix_unbounded);
	}

	kind = locklint_get_execution_annotation(insn);
	if (kind == LOCKLINT_EXECUTION_NO_COMPETITION)
		change_competition_state(state, -1);
	else if (kind == LOCKLINT_EXECUTION_COMPETITION)
		change_competition_state(state, 1);
	else
		return;
	update_competition_minimum_prefix(state, minimum_prefix,
	    minimum_prefix_unbounded);
}

static void
merge_competition_transfer_prefix(int *minimum, bool *unbounded,
    int incoming_minimum, bool incoming_unbounded)
{
	if (incoming_unbounded) {
		*minimum = 0;
		*unbounded = true;
	} else if (!*unbounded && incoming_minimum < *minimum) {
		*minimum = incoming_minimum;
	}
}

static struct competition_transfer_block_info *
solve_competition_transfer_blocks(struct function_info *function,
    const struct competition_state *input)
{
	struct competition_transfer_block_info *blocks = NULL;
	struct competition_transfer_block_info **tail = &blocks;
	struct competition_transfer_block_info *block;
	struct basic_block *bb;
	bool changed;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		block = calloc(1, sizeof (*block));
		if (block == NULL)
			die("out of memory summarizing competition transfer");
		block->bb = bb;
		*tail = block;
		tail = &block->next;
	} END_FOR_EACH_PTR(bb);

	do {
		changed = false;
		for (block = blocks; block != NULL; block = block->next) {
			struct competition_state in = *input;
			struct competition_state out;
			struct basic_block *parent;
			int minimum_prefix_in = 0;
			int minimum_prefix_out;
			bool minimum_prefix_in_unbounded = false;
			bool minimum_prefix_out_unbounded;
			bool reachable = block->bb == function->ep->entry->bb;
			bool complete = true;
			bool first = true;
			struct instruction *insn;

			if (!reachable) {
				FOR_EACH_PTR(block->bb->parents, parent) {
					struct competition_transfer_block_info
					    *parent_block;

					parent_block =
					    find_competition_transfer_block(blocks,
					    parent);
					if (parent_block == NULL ||
					    !parent_block->reachable) {
						complete = false;
						continue;
					}
					if (first) {
						in = parent_block->out;
						minimum_prefix_in =
						    parent_block->
						    minimum_prefix_out;
						minimum_prefix_in_unbounded =
						    parent_block->
						    minimum_prefix_out_unbounded;
						first = false;
					} else {
						merge_competition_state(&in,
						    &parent_block->out);
						merge_competition_transfer_prefix(
						    &minimum_prefix_in,
						    &minimum_prefix_in_unbounded,
						    parent_block->
						    minimum_prefix_out,
						    parent_block->
						    minimum_prefix_out_unbounded);
					}
					reachable = true;
				} END_FOR_EACH_PTR(parent);
			}
			if (!reachable)
				continue;
			if (block->input_complete && complete) {
				widen_competition_state(&in, &block->in);
				if (block->minimum_prefix_in_unbounded ||
				    (!minimum_prefix_in_unbounded &&
				    minimum_prefix_in <
				    block->minimum_prefix_in)) {
					minimum_prefix_in = 0;
					minimum_prefix_in_unbounded = true;
				}
			}
			out = in;
			minimum_prefix_out = minimum_prefix_in;
			minimum_prefix_out_unbounded =
			    minimum_prefix_in_unbounded;
			FOR_EACH_PTR(block->bb->insns, insn) {
				if (insn->bb != NULL) {
					simulate_competition_transfer_instruction(
					    function, &out,
					    &minimum_prefix_out,
					    &minimum_prefix_out_unbounded, insn);
				}
			} END_FOR_EACH_PTR(insn);
			if (!block->reachable ||
			    !same_competition_state(&block->in, &in) ||
			    !same_competition_state(&block->out, &out) ||
			    block->minimum_prefix_in != minimum_prefix_in ||
			    block->minimum_prefix_out != minimum_prefix_out ||
			    block->minimum_prefix_in_unbounded !=
			    minimum_prefix_in_unbounded ||
			    block->minimum_prefix_out_unbounded !=
			    minimum_prefix_out_unbounded)
				changed = true;
			block->reachable = true;
			block->input_complete = complete;
			block->in = in;
			block->out = out;
			block->minimum_prefix_in = minimum_prefix_in;
			block->minimum_prefix_out = minimum_prefix_out;
			block->minimum_prefix_in_unbounded =
			    minimum_prefix_in_unbounded;
			block->minimum_prefix_out_unbounded =
			    minimum_prefix_out_unbounded;
		}
	} while (changed);
	return (blocks);
}

static struct competition_state
simulate_competition_transfer(struct function_info *function,
    const struct competition_state *input, int *minimum_prefix,
    bool *minimum_prefix_unbounded)
{
	struct competition_transfer_block_info *blocks;
	struct competition_transfer_block_info *block;
	struct competition_state result = *input;
	bool found_return = false;

	*minimum_prefix = 0;
	*minimum_prefix_unbounded = false;
	blocks = solve_competition_transfer_blocks(function, input);
	for (block = blocks; block != NULL; block = block->next) {
		if (!block->reachable)
			continue;
		merge_competition_transfer_prefix(minimum_prefix,
		    minimum_prefix_unbounded, block->minimum_prefix_out,
		    block->minimum_prefix_out_unbounded);
	}
	for (block = blocks; block != NULL; block = block->next) {
		struct instruction *insn;
		bool returns = false;

		if (!block->reachable)
			continue;
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb != NULL && insn->opcode == OP_RET)
				returns = true;
		} END_FOR_EACH_PTR(insn);
		if (!returns)
			continue;
		if (!found_return) {
			result = block->out;
			found_return = true;
		} else {
			merge_competition_state(&result, &block->out);
		}
	}
	while (blocks != NULL) {
		struct competition_transfer_block_info *next = blocks->next;

		free(blocks);
		blocks = next;
	}
	return (result);
}

static bool
solve_function_competition_transfer(struct function_info *function,
    bool widen)
{
	const struct competition_state exact_input = { 0 };
	const struct competition_state ambient_input = {
		.minimum = 0,
		.maximum = 1,
		.ambient = true
	};
	struct competition_transfer *transfer = function->competition_transfer;
	struct competition_state output;
	struct competition_state ambient_output;
	int minimum_prefix;
	int ambient_minimum_prefix;
	bool minimum_prefix_unbounded;
	bool ambient_minimum_prefix_unbounded;

	output = simulate_competition_transfer(function, &exact_input,
	    &minimum_prefix, &minimum_prefix_unbounded);
	ambient_output = simulate_competition_transfer(function, &ambient_input,
	    &ambient_minimum_prefix, &ambient_minimum_prefix_unbounded);
	if (widen) {
		merge_competition_state(&output, &transfer->output);
		widen_competition_state(&output, &transfer->output);
		merge_competition_state(&ambient_output,
		    &transfer->ambient_output);
		widen_competition_state(&ambient_output,
		    &transfer->ambient_output);
		if (transfer->minimum_prefix_unbounded ||
		    (!minimum_prefix_unbounded &&
		    minimum_prefix < transfer->minimum_prefix)) {
			minimum_prefix = 0;
			minimum_prefix_unbounded = true;
		} else if (minimum_prefix > transfer->minimum_prefix) {
			minimum_prefix = transfer->minimum_prefix;
		}
		if (transfer->ambient_minimum_prefix_unbounded ||
		    (!ambient_minimum_prefix_unbounded &&
		    ambient_minimum_prefix <
		    transfer->ambient_minimum_prefix)) {
			ambient_minimum_prefix = 0;
			ambient_minimum_prefix_unbounded = true;
		} else if (ambient_minimum_prefix >
		    transfer->ambient_minimum_prefix) {
			ambient_minimum_prefix =
			    transfer->ambient_minimum_prefix;
		}
	}
	if (same_competition_state(&transfer->output, &output) &&
	    same_competition_state(&transfer->ambient_output,
	    &ambient_output) &&
	    transfer->minimum_prefix == minimum_prefix &&
	    transfer->minimum_prefix_unbounded ==
	    minimum_prefix_unbounded &&
	    transfer->ambient_minimum_prefix == ambient_minimum_prefix &&
	    transfer->ambient_minimum_prefix_unbounded ==
	    ambient_minimum_prefix_unbounded)
		return (false);
	transfer->output = output;
	transfer->ambient_output = ambient_output;
	transfer->minimum_prefix = minimum_prefix;
	transfer->minimum_prefix_unbounded = minimum_prefix_unbounded;
	transfer->ambient_minimum_prefix = ambient_minimum_prefix;
	transfer->ambient_minimum_prefix_unbounded =
	    ambient_minimum_prefix_unbounded;
	return (true);
}

static bool
formal_argument(struct entrypoint *ep, struct symbol *symbol,
    unsigned int *index)
{
	struct symbol *type = ep->name->ctype.base_type;
	struct symbol *argument;
	unsigned int current = 0;

	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type == NULL || type->type != SYM_FN)
		return (false);
	FOR_EACH_PTR(type->arguments, argument) {
		if (argument == symbol) {
			*index = current;
			return (true);
		}
		current++;
	} END_FOR_EACH_PTR(argument);
	return (false);
}

static struct expression *
call_argument(struct instruction *insn, unsigned int index)
{
	struct expression *argument;
	unsigned int current = 0;

	if (insn->call_expr == NULL)
		return (NULL);
	FOR_EACH_PTR(insn->call_expr->args, argument) {
		if (current++ == index)
			return (argument);
	} END_FOR_EACH_PTR(argument);
	return (NULL);
}

static struct symbol *
argument_member(struct expression *argument, struct symbol *member)
{
	struct symbol *type;
	int offset = 0;

	if (member == NULL || member->ident == NULL || argument == NULL)
		return (member);
	type = argument->ctype;
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type != NULL && type->type == SYM_PTR)
		type = get_base_type(type);
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type == NULL ||
	    (type->type != SYM_STRUCT && type->type != SYM_UNION))
		return (member);
	examine_symbol_type(type);
	type = find_identifier(member->ident, type->symbol_list, &offset);
	return (type != NULL ? type : member);
}

static bool
argument_lock(struct translation_unit *tu, struct instruction *insn,
    unsigned int index, struct symbol *member, unsigned long offset,
    struct locklint_access *lock)
{
	struct locklint_access base = { 0 };
	struct locklint_access relative = { 0 };
	struct expression *argument = call_argument(insn, index);

	if (!locklint_get_call_argument_access(tu, insn, index, &base))
		return (false);
	relative.member = argument_member(argument, member);
	relative.offset = offset;
	locklint_rebase_access(&base, &relative, lock);
	lock->path = NULL;
	return (true);
}

static bool
acquisition_role(struct function_info *function,
    const struct locklint_access *lock, struct acquisition_role *role)
{
	*role = (struct acquisition_role){ 0 };
	role->access = *lock;
	if (formal_argument(function->ep, lock->root, &role->argument)) {
		role->relative = true;
	} else if (lock->address_base != NULL &&
	    lock->address_base->type == PSEUDO_ARG) {
		role->relative = true;
		role->argument = lock->address_base->nr;
		role->access.root = formal_symbol(function->ep, role->argument);
		role->access.object = NULL;
	} else if (lock->object == NULL) {
		return (false);
	}
	role->access.expr = NULL;
	role->access.address_base = NULL;
	role->access.address_offset = 0;
	return (true);
}

static bool
same_acquisition_role(const struct acquisition_role *left,
    const struct acquisition_role *right)
{
	if (left->relative != right->relative)
		return (false);
	if (left->relative) {
		return (left->argument == right->argument &&
		    left->access.member == right->access.member &&
		    left->access.offset == right->access.offset);
	}
	return (locklint_same_access(&left->access, &right->access));
}

static bool
map_acquisition_role(struct function_info *function,
    struct instruction *insn, const struct acquisition_role *role,
    struct locklint_access *lock)
{
	if (!role->relative) {
		*lock = role->access;
		return (true);
	}
	return (argument_lock(function->tu, insn, role->argument,
	    role->access.member, role->access.offset, lock));
}

static bool
condition_alternative_satisfied(struct function_info *function,
    struct instruction *insn, const struct protection_condition *condition,
    const struct locklint_access *required)
{
	const struct protection_alternative *alternative;

	for (alternative = condition->alternatives; alternative != NULL;
	    alternative = alternative->next) {
		struct locklint_access candidate;

		if (argument_lock(function->tu, insn, alternative->argument,
		    alternative->lock_member, alternative->lock_offset,
		    &candidate) && same_lock(required, &candidate))
			return (true);
	}
	return (false);
}

/*
 * Map the protected object independently from its required lock.  A relative
 * lock follows the caller's actual object; an absolute lock keeps its
 * program-wide identity.
 */
static bool
map_call_protection_condition(struct function_info *function,
    struct instruction *insn, const struct protection_condition *condition,
    struct locklint_access *object, struct locklint_access *lock)
{
	struct locklint_access base = { 0 };
	struct expression *argument;

	*object = (struct locklint_access){ 0 };
	*lock = (struct locklint_access){ 0 };
	if (condition->data_root == NULL) {
		argument = call_argument(insn, condition->argument);
		if (!locklint_get_call_argument_access(function->tu, insn,
		    condition->argument, &base))
			return (false);
	} else {
		argument = NULL;
		base.root = condition->data_root;
		base.object = condition->data_object;
		base.type = condition->data_root->ctype.base_type;
		base.member = NULL;
		base.offset = 0;
		base.expr = NULL;
		base.path = NULL;
	}
	if (condition->has_lock && condition->lock_root == NULL) {
		struct locklint_access relative = { 0 };

		/* Relative locks move with the selected data object's base. */
		relative.member = argument_member(argument,
		    condition->lock_member);
		relative.offset = condition->lock_offset;
		locklint_rebase_access(&base, &relative, lock);
		lock->path = NULL;
	} else if (condition->has_lock) {
		/* Absolute locks keep their original program-wide identity. */
		lock->root = condition->lock_root;
		lock->object = condition->lock_object;
		lock->type = condition->lock_root->ctype.base_type;
		lock->member = condition->lock_member;
		lock->offset = condition->lock_offset;
		lock->expr = NULL;
		lock->path = NULL;
	}
	{
		struct locklint_access relative = { 0 };

		relative.member = argument_member(argument,
		    condition->data_member);
		relative.offset = condition->data_offset;
		locklint_rebase_access(&base, &relative, object);
	}
	object->path = NULL;
	return (true);
}

static struct symbol *
formal_symbol(struct entrypoint *ep, unsigned int index)
{
	struct symbol *type = ep->name->ctype.base_type;
	struct symbol *argument;
	unsigned int current = 0;

	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type == NULL || type->type != SYM_FN)
		return (NULL);
	FOR_EACH_PTR(type->arguments, argument) {
		if (current++ == index)
			return (argument);
	} END_FOR_EACH_PTR(argument);
	return (NULL);
}

static struct lock_transfer *
add_transfer(struct function_info *function,
    const struct acquisition_role *role, bool *added)
{
	struct lock_transfer *transfer;
	unsigned int state;

	for (transfer = function->transfers; transfer != NULL;
	    transfer = transfer->next) {
		if (same_acquisition_role(&transfer->role, role)) {
			*added = false;
			return (transfer);
		}
	}
	transfer = calloc(1, sizeof (*transfer));
	if (transfer == NULL)
		die("out of memory recording lock transfer");
	transfer->role = *role;
	for (state = 0; state < LOCK_STATE_COUNT; state++)
		transfer->output[state] = state;
	transfer->next = function->transfers;
	function->transfers = transfer;
	*added = true;
	return (transfer);
}

static struct lock_transfer *
find_transfer(struct function_info *function,
    const struct acquisition_role *role)
{
	struct lock_transfer *transfer;

	for (transfer = function->transfers; transfer != NULL;
	    transfer = transfer->next) {
		if (same_acquisition_role(&transfer->role, role))
			return (transfer);
	}
	return (NULL);
}

static struct mapped_lock_transfer *
map_call_lock_transfers(struct function_info *function,
    struct instruction *insn, struct function_info *callee)
{
	struct mapped_lock_transfer *mapped = NULL;
	struct mapped_lock_transfer **mapped_tail = &mapped;
	struct lock_transfer *transfer;

	for (transfer = callee->transfers; transfer != NULL;
	    transfer = transfer->next) {
		struct mapped_lock_transfer *group;
		struct mapped_transfer_role *role;
		struct locklint_access lock;

		if (!map_acquisition_role(function, insn, &transfer->role,
		    &lock))
			continue;
		for (group = mapped; group != NULL; group = group->next)
			if (same_lock(&group->lock, &lock))
				break;
		if (group == NULL) {
			group = calloc(1, sizeof (*group));
			if (group == NULL)
				die("out of memory mapping lock transfers");
			group->lock = lock;
			*mapped_tail = group;
			mapped_tail = &group->next;
		}
		role = calloc(1, sizeof (*role));
		if (role == NULL)
			die("out of memory mapping lock transfer role");
		role->transfer = transfer;
		if (group->roles == NULL) {
			group->roles = role;
		} else {
			struct mapped_transfer_role *last = group->roles;

			while (last->next != NULL)
				last = last->next;
			last->next = role;
		}
		group->role_count++;
	}
	return (mapped);
}

static void
free_mapped_transfer_roles(struct mapped_transfer_role *roles)
{
	while (roles != NULL) {
		struct mapped_transfer_role *next = roles->next;

		free(roles);
		roles = next;
	}
}

static void
free_mapped_lock_transfers(struct mapped_lock_transfer *mapped)
{
	while (mapped != NULL) {
		struct mapped_lock_transfer *next = mapped->next;

		free_mapped_transfer_roles(mapped->roles);
		free(mapped);
		mapped = next;
	}
}

static void
transfer_call_effects(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	struct function_info *callee = callgraph_callee(function, insn);
	struct mapped_lock_transfer *mapped;
	struct mapped_lock_transfer *group;

	if (callee == NULL)
		return;
	if (callee->transfers != NULL && callee->transfers->next == NULL) {
		struct lock_transfer *transfer = callee->transfers;
		struct locklint_access lock;
		lock_state_t state;

		if (!map_acquisition_role(function, insn, &transfer->role,
		    &lock))
			return;
		state = get_state(*states, &lock);
		set_state(states, &lock, transfer->output[state]);
		return;
	}
	mapped = map_call_lock_transfers(function, insn, callee);
	for (group = mapped; group != NULL; group = group->next) {
		lock_state_t state;

		state = get_state(*states, &group->lock);
		if (group->role_count == 1) {
			state = group->roles->transfer->output[state];
		} else {
			unsigned int invalid;

			state = simulate_aliased_transfer(callee, group->roles,
			    state, &invalid);
		}
		set_state(states, &group->lock, state);
	}
	free_mapped_lock_transfers(mapped);
}

static bool
same_condition_lock(const struct protection_condition *condition,
    bool has_lock, unsigned int required_modes,
    struct symbol *root, struct object_identity *object,
    struct symbol *member, unsigned long offset)
{
	struct locklint_access left = { 0 };
	struct locklint_access right = { 0 };

	if (condition->has_lock != has_lock)
		return (false);
	if (!has_lock)
		return (true);
	if (condition->required_modes != required_modes)
		return (false);
	/* Different declaration symbols can still denote one absolute lock. */
	left.root = condition->lock_root;
	left.object = condition->lock_object;
	left.member = condition->lock_member;
	left.offset = condition->lock_offset;
	right.root = root;
	right.object = object;
	right.member = member;
	right.offset = offset;
	return (same_lock(&left, &right));
}

static bool
same_condition_data(const struct protection_condition *condition,
    struct symbol *root, struct object_identity *object,
    struct symbol *member, unsigned long offset)
{
	struct locklint_access left = { 0 };
	struct locklint_access right = { 0 };

	left.root = condition->data_root;
	left.object = condition->data_object;
	left.member = condition->data_member;
	left.offset = condition->data_offset;
	right.root = root;
	right.object = object;
	right.member = member;
	right.offset = offset;
	return (locklint_same_access(&left, &right));
}

static bool
same_protection_alternatives(const struct protection_alternative *left,
    const struct protection_alternative *right)
{
	const struct protection_alternative *alternative;
	unsigned int left_count = 0;
	unsigned int right_count = 0;

	for (alternative = left; alternative != NULL;
	    alternative = alternative->next)
		left_count++;
	for (alternative = right; alternative != NULL;
	    alternative = alternative->next)
		right_count++;
	if (left_count != right_count)
		return (false);
	for (; left != NULL; left = left->next) {
		for (alternative = right; alternative != NULL;
		    alternative = alternative->next) {
			if (left->argument == alternative->argument &&
			    left->lock_member == alternative->lock_member &&
			    left->lock_offset == alternative->lock_offset)
				break;
		}
		if (alternative == NULL)
			return (false);
	}
	return (true);
}

static void
free_protection_alternatives(struct protection_alternative *alternatives)
{
	while (alternatives != NULL) {
		struct protection_alternative *next = alternatives->next;

		free(alternatives);
		alternatives = next;
	}
}

static bool
add_protection_alternative(struct protection_alternative **alternatives,
    unsigned int argument, struct symbol *lock_member,
    unsigned long lock_offset)
{
	struct protection_alternative *alternative;

	for (alternative = *alternatives; alternative != NULL;
	    alternative = alternative->next) {
		if (alternative->argument == argument &&
		    alternative->lock_member == lock_member &&
		    alternative->lock_offset == lock_offset)
			return (false);
	}
	alternative = calloc(1, sizeof (*alternative));
	if (alternative == NULL)
		die("out of memory recording protection alternative");
	alternative->argument = argument;
	alternative->lock_member = lock_member;
	alternative->lock_offset = lock_offset;
	alternative->next = *alternatives;
	*alternatives = alternative;
	return (true);
}

static struct protection_alternative *
copy_protection_alternatives(
    const struct protection_alternative *alternatives)
{
	struct protection_alternative *copy = NULL;

	for (; alternatives != NULL; alternatives = alternatives->next) {
		(void) add_protection_alternative(&copy,
		    alternatives->argument, alternatives->lock_member,
		    alternatives->lock_offset);
	}
	return (copy);
}

static bool
add_protection_condition(struct function_info *function,
    unsigned int argument, struct symbol *data_root,
    struct object_identity *data_object,
    bool has_lock, unsigned int required_modes,
    struct symbol *lock_root, struct object_identity *lock_object,
    struct symbol *lock_member, unsigned long lock_offset,
    struct symbol *data_member, unsigned long data_offset,
    struct position pos,
    const struct protection_alternative *alternatives)
{
	struct protection_condition *condition;

	for (condition = function->conditions; condition != NULL;
	    condition = condition->next) {
		if (condition->argument == argument &&
		    same_condition_data(condition, data_root, data_object,
		    data_member, data_offset) &&
		    same_condition_lock(condition, has_lock, required_modes,
		    lock_root,
		    lock_object, lock_member, lock_offset) &&
		    same_protection_alternatives(condition->alternatives,
		    alternatives)) {
			return (false);
		}
	}
	condition = calloc(1, sizeof (*condition));
	if (condition == NULL)
		die("out of memory recording protection condition");
	condition->argument = argument;
	condition->data_root = data_root;
	condition->data_object = data_object;
	condition->has_lock = has_lock;
	condition->required_modes = required_modes;
	condition->lock_root = lock_root;
	condition->lock_object = lock_object;
	condition->lock_member = lock_member;
	condition->lock_offset = lock_offset;
	condition->data_member = data_member;
	condition->data_offset = data_offset;
	condition->pos = pos;
	condition->alternatives =
	    copy_protection_alternatives(alternatives);
	condition->next = function->conditions;
	function->conditions = condition;
	return (true);
}

static struct protection_alternative *
collect_state_protection_alternatives(struct function_info *function,
    unsigned int required_modes, const struct analysis_state *state)
{
	const struct state_entry *entry;
	struct protection_alternative *alternatives = NULL;

	for (entry = state->locks; entry != NULL; entry = entry->next) {
		unsigned int argument;

		if (!state_satisfies_modes(entry->state, required_modes) ||
		    !formal_argument(function->ep, entry->lock.root, &argument))
			continue;
		(void) add_protection_alternative(&alternatives, argument,
		    entry->lock.member, entry->lock.offset);
	}
	return (alternatives);
}

static bool
defer_conditions(struct function_info *function)
{
	return (function->reachable_from_root && function->root_reasons == 0);
}

static bool
defer_lock_diagnostics(struct function_info *function,
    const struct locklint_access *lock)
{
	unsigned int argument;

	return (defer_conditions(function) &&
	    (formal_argument(function->ep, lock->root, &argument) ||
	    function_declares_lock_effect(function, lock)));
}

static bool
condition_data_identity(struct function_info *function,
    const struct locklint_access *access, unsigned int *argument,
    struct symbol **root, struct object_identity **object)
{
	if (formal_argument(function->ep, access->root, argument)) {
		*root = NULL;
		*object = NULL;
		return (true);
	}
	if (access->object == NULL)
		return (false);
	*argument = 0;
	*root = access->root;
	*object = access->object;
	return (true);
}

static void
visibility_transfer_access(const struct function_info *function,
    const struct visibility_transfer *transfer, struct locklint_access *region)
{
	*region = (struct locklint_access){ 0 };
	if (transfer->data_root == NULL) {
		region->root = formal_symbol(function->ep, transfer->argument);
	} else {
		region->root = transfer->data_root;
		region->object = transfer->data_object;
		region->type = transfer->data_root->ctype.base_type;
	}
	region->member = transfer->data_member;
	region->offset = transfer->data_offset;
	region->path = transfer->data_path;
}

static bool
map_call_visibility_transfer(struct function_info *function,
    struct instruction *insn, const struct visibility_transfer *transfer,
    struct locklint_access *region)
{
	struct locklint_access base = { 0 };
	struct locklint_access relative;

	if (transfer->data_root == NULL) {
		if (!locklint_get_call_argument_access(function->tu, insn,
		    transfer->argument, &base))
			return (false);
		relative = (struct locklint_access){ 0 };
		relative.member = transfer->data_member;
		relative.offset = transfer->data_offset;
		relative.path = transfer->data_path;
		locklint_rebase_access(&base, &relative, region);
	} else {
		*region = (struct locklint_access){ 0 };
		region->root = transfer->data_root;
		region->object = transfer->data_object;
		region->type = transfer->data_root->ctype.base_type;
		region->member = transfer->data_member;
		region->offset = transfer->data_offset;
		region->expr = NULL;
		region->path = transfer->data_path;
	}
	return (true);
}

static struct visibility_transfer *
add_visibility_transfer(struct function_info *function,
    unsigned int argument, struct symbol *data_root,
    struct object_identity *data_object, struct symbol *data_member,
    unsigned long data_offset, struct locklint_member_path *data_path,
    bool *added)
{
	struct visibility_transfer *transfer;
	struct locklint_access candidate = { 0 };
	unsigned int state;

	candidate.root = data_root;
	candidate.object = data_object;
	candidate.member = data_member;
	candidate.offset = data_offset;
	candidate.path = data_path;
	for (transfer = function->visibility_transfers; transfer != NULL;
	    transfer = transfer->next) {
		struct locklint_access existing = { 0 };

		existing.root = transfer->data_root;
		existing.object = transfer->data_object;
		existing.member = transfer->data_member;
		existing.offset = transfer->data_offset;
		existing.path = transfer->data_path;
		if (transfer->argument == argument &&
		    locklint_same_access(&existing, &candidate)) {
			*added = false;
			return (transfer);
		}
	}
	transfer = calloc(1, sizeof (*transfer));
	if (transfer == NULL)
		die("out of memory recording visibility transfer");
	transfer->argument = argument;
	transfer->data_root = data_root;
	transfer->data_object = data_object;
	transfer->data_member = data_member;
	transfer->data_offset = data_offset;
	transfer->data_path = data_path;
	for (state = 0; state < VISIBILITY_STATE_COUNT; state++)
		transfer->output[state] = state;
	transfer->next = function->visibility_transfers;
	function->visibility_transfers = transfer;
	*added = true;
	return (transfer);
}

static void
free_mapped_visibility_effects(struct mapped_visibility_effect *effects)
{
	while (effects != NULL) {
		struct mapped_visibility_effect *next = effects->next;

		free(effects);
		effects = next;
	}
}

static struct mapped_visibility_effect *
map_call_visibility_effects(struct function_info *function,
    struct instruction *insn, struct function_info *callee)
{
	struct mapped_visibility_effect *effects = NULL;
	struct visibility_transfer *transfer;

	for (transfer = callee->visibility_transfers; transfer != NULL;
	    transfer = transfer->next) {
		struct mapped_visibility_effect **link;
		struct mapped_visibility_effect *effect;
		struct locklint_access region;
		unsigned int state;

		if (!map_call_visibility_transfer(function, insn, transfer,
		    &region))
			continue;
		for (effect = effects; effect != NULL; effect = effect->next) {
			if (!locklint_same_access(&effect->region, &region))
				continue;
			for (state = 0; state < VISIBILITY_STATE_COUNT; state++) {
				effect->output[state] = merge_visibility_state(
				    effect->output[state],
				    transfer->output[state]);
			}
			break;
		}
		if (effect != NULL)
			continue;
		effect = calloc(1, sizeof (*effect));
		if (effect == NULL)
			die("out of memory mapping visibility effects");
		effect->region = region;
		for (state = 0; state < VISIBILITY_STATE_COUNT; state++)
			effect->output[state] = transfer->output[state];
		for (link = &effects; *link != NULL; link = &(*link)->next)
			if (locklint_access_depth(&region) <
			    locklint_access_depth(&(*link)->region))
				break;
		effect->next = *link;
		*link = effect;
	}
	return (effects);
}

static void
add_visibility_result(struct visibility_entry **results,
    const struct locklint_access *region, enum visibility_state state)
{
	struct visibility_entry **link;
	struct visibility_entry *entry;

	for (entry = *results; entry != NULL; entry = entry->next) {
		if (locklint_same_access(&entry->region, region)) {
			entry->state = state;
			return;
		}
	}
	entry = alloc_visibility(region, state);
	for (link = results; *link != NULL; link = &(*link)->next)
		if (locklint_access_depth(region) <
		    locklint_access_depth(&(*link)->region))
			break;
	entry->next = *link;
	*link = entry;
}

static void
transfer_call_visibility_effects(struct function_info *function,
    struct visibility_entry **visibility, struct instruction *insn)
{
	struct function_info *callee = callgraph_callee(function, insn);
	struct mapped_visibility_effect *effects;
	struct mapped_visibility_effect *effect;
	struct visibility_entry *original;
	struct visibility_entry *results = NULL;
	struct visibility_entry *entry;

	if (callee == NULL)
		return;
	effects = map_call_visibility_effects(function, insn, callee);
	original = copy_visibility(*visibility);
	for (effect = effects; effect != NULL; effect = effect->next) {
		enum visibility_state state;

		state = get_visibility(original, &effect->region);
		add_visibility_result(&results, &effect->region,
		    effect->output[state]);
	}
	for (entry = original; entry != NULL; entry = entry->next) {
		struct mapped_visibility_effect *selected = NULL;

		for (effect = effects; effect != NULL; effect = effect->next) {
			if (locklint_access_contains(&effect->region,
			    &entry->region))
				selected = effect;
		}
		if (selected != NULL)
			add_visibility_result(&results, &entry->region,
			    selected->output[entry->state]);
	}
	for (entry = results; entry != NULL; entry = entry->next)
		set_visibility(visibility, &entry->region, entry->state);
	free_visibility(results);
	free_visibility(original);
	free_mapped_visibility_effects(effects);
}

static const char *
lock_name(const struct locklint_access *lock)
{
	if (lock->member != NULL && lock->member->ident != NULL)
		return (show_ident(lock->member->ident));
	if (lock->root != NULL && lock->root->ident != NULL)
		return (show_ident(lock->root->ident));
	return ("<unknown>");
}

static const char *
access_name(const struct locklint_access *access)
{
	struct symbol *symbol = access->member != NULL ?
	    access->member : access->root;

	return (symbol != NULL && symbol->ident != NULL ?
	    show_ident(symbol->ident) : "<unknown>");
}

static const char *
required_ownership(unsigned int required_modes)
{
	if (required_modes == LOCK_WRITE_HELD)
		return ("write-holding");
	if (required_modes == (LOCK_READ_HELD | LOCK_WRITE_HELD))
		return ("read-holding");
	return ("holding");
}

static bool
assumed_protected(const struct function_info *function,
    const struct locklint_access *access)
{
	struct assumed_region *assumption;

	for (assumption = function->assumptions; assumption != NULL;
	    assumption = assumption->next) {
		if (locklint_access_contains(&assumption->region, access))
			return (true);
	}
	return (false);
}

static enum protection_status
get_protection_status(const struct function_info *function,
    const struct analysis_state *state,
    const struct locklint_access *object, unsigned int required_modes,
    const struct locklint_access *lock)
{
	lock_state_t lock_state = required_modes != 0 ?
	    get_state(state->locks, lock) : LOCK_NOT_HELD;
	enum visibility_state visibility =
	    get_visibility(state->visibility, object);
	bool lock_definite = (lock_state & required_modes) != 0 &&
	    (lock_state & ~required_modes) == 0;
	bool lock_partial = (lock_state & required_modes) != 0 &&
	    (lock_state & ~required_modes) != 0;

	if (assumed_protected(function, object) ||
	    lock_definite ||
	    visibility == VISIBILITY_INVISIBLE ||
	    competition_absent(&state->competition))
		return (PROTECTION_DEFINITE);
	if (lock_partial ||
	    visibility == VISIBILITY_MAYBE ||
	    (state->competition.path_dependent &&
	    !competition_present(&state->competition)))
		return (PROTECTION_PATH_DEPENDENT);
	return (PROTECTION_ABSENT);
}

static bool
protected_access(struct function_info *function, struct instruction *insn,
    struct locklint_access *access, struct locklint_access *lock,
    struct symbol **data_member, unsigned int *required_modes)
{
	struct locklint_data_policy policy;

	if (!locklint_get_instruction_access(function->tu, insn, access))
		return (false);
	if (!locklint_data_policy(access, &policy, lock) ||
	    (policy.protection != LOCKLINT_PROTECTION_MUTEX &&
	    policy.protection != LOCKLINT_PROTECTION_RWLOCK) ||
	    (insn->opcode == OP_LOAD && policy.readable_without_lock))
		return (false);
	if (policy.protection == LOCKLINT_PROTECTION_MUTEX)
		*required_modes = LOCK_HELD;
	else if (insn->opcode == OP_LOAD)
		*required_modes = LOCK_READ_HELD | LOCK_WRITE_HELD;
	else
		*required_modes = LOCK_WRITE_HELD;
	*data_member = access->member != NULL ?
	    access->member : access->root;
	return (true);
}

static bool
collect_local_transfers(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct acquisition_role role;
			struct locklint_access lock;
			enum locklint_declared_lock_effect effect;
			enum locklint_lock_action action;
			enum locklint_lock_mode mode;
			bool added;

			if (insn->bb == NULL)
				continue;
			action = locklint_get_lock_action(function->tu, insn,
			    &lock, &mode);
			if (action == LOCKLINT_LOCK_NONE) {
				if (!locklint_get_declared_lock_effect(function->tu,
				    insn, &effect, &lock))
					continue;
			}
			if (!acquisition_role(function, &lock, &role))
				continue;
			(void) add_transfer(function, &role, &added);
			if (added)
				changed = true;
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
}

enum declared_effect_status {
	DECLARED_EFFECT_MATCHES,
	DECLARED_EFFECT_CONDITIONAL,
	DECLARED_EFFECT_CONFLICTS
};

static const char *
declared_effect_description(enum locklint_declared_lock_effect effect)
{
	switch (effect) {
	case LOCKLINT_DECLARED_MUTEX_ACQUIRED:
		return ("mutex acquisition");
	case LOCKLINT_DECLARED_READ_ACQUIRED:
		return ("read-lock acquisition");
	case LOCKLINT_DECLARED_WRITE_ACQUIRED:
		return ("write-lock acquisition");
	case LOCKLINT_DECLARED_LOCK_RELEASED:
		return ("release");
	default:
		abort();
	}
}

static enum declared_effect_status
declared_output_status(lock_state_t output, lock_state_t expected)
{
	if (output == expected)
		return (DECLARED_EFFECT_MATCHES);
	if ((output & expected) != 0)
		return (DECLARED_EFFECT_CONDITIONAL);
	return (DECLARED_EFFECT_CONFLICTS);
}

static enum declared_effect_status
declared_effect_status(struct lock_transfer *transfer,
    enum locklint_declared_lock_effect effect)
{
	static const lock_state_t held_inputs[] = {
		LOCK_HELD,
		LOCK_READ_HELD,
		LOCK_WRITE_HELD
	};
	enum declared_effect_status status = DECLARED_EFFECT_MATCHES;
	lock_state_t expected;
	unsigned int i;

	switch (effect) {
	case LOCKLINT_DECLARED_MUTEX_ACQUIRED:
		return (declared_output_status(
		    transfer->output[LOCK_NOT_HELD], LOCK_HELD));
	case LOCKLINT_DECLARED_READ_ACQUIRED:
		return (declared_output_status(
		    transfer->output[LOCK_NOT_HELD], LOCK_READ_HELD));
	case LOCKLINT_DECLARED_WRITE_ACQUIRED:
		return (declared_output_status(
		    transfer->output[LOCK_NOT_HELD], LOCK_WRITE_HELD));
	case LOCKLINT_DECLARED_LOCK_RELEASED:
		expected = LOCK_NOT_HELD;
		break;
	default:
		abort();
	}

	for (i = 0; i < sizeof (held_inputs) / sizeof (held_inputs[0]); i++) {
		enum declared_effect_status current;

		current = declared_output_status(
		    transfer->output[held_inputs[i]], expected);
		if (current == DECLARED_EFFECT_CONFLICTS)
			return (current);
		if (current == DECLARED_EFFECT_CONDITIONAL)
			status = current;
	}
	return (status);
}

static void
validate_declared_effects(struct function_info *function)
{
	struct basic_block *bb;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct acquisition_role role;
			struct locklint_access lock;
			enum locklint_declared_lock_effect effect;
			enum declared_effect_status status;
			struct lock_transfer *transfer;

			if (insn->bb == NULL ||
			    !locklint_get_declared_lock_effect(function->tu,
			    insn, &effect, &lock) ||
			    !acquisition_role(function, &lock, &role))
				continue;
			transfer = find_transfer(function, &role);
			if (transfer == NULL)
				abort();
			status = declared_effect_status(transfer, effect);
			if (status == DECLARED_EFFECT_MATCHES)
				continue;
			if (status == DECLARED_EFFECT_CONDITIONAL) {
				warning(insn->context_expr->pos,
				    "locklint: declared %s of lock '%s' is not "
				    "established on every return from '%s'",
				    declared_effect_description(effect),
				    lock_name(&lock),
				    show_ident(function->ep->name->ident));
			} else {
				warning(insn->context_expr->pos,
				    "locklint: function '%s' does not establish "
				    "declared %s of lock '%s'",
				    show_ident(function->ep->name->ident),
				    declared_effect_description(effect),
				    lock_name(&lock));
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static bool
competition_output_contains(const struct competition_state *output,
    int expected)
{
	return ((output->minimum_unbounded || output->minimum <= expected) &&
	    (output->maximum_unbounded || output->maximum >= expected));
}

static enum declared_effect_status
declared_competition_effect_status(const struct competition_state *output,
    int expected)
{
	if (!output->minimum_unbounded && !output->maximum_unbounded &&
	    output->minimum == expected && output->maximum == expected)
		return (DECLARED_EFFECT_MATCHES);
	if (competition_output_contains(output, expected))
		return (DECLARED_EFFECT_CONDITIONAL);
	return (DECLARED_EFFECT_CONFLICTS);
}

static void
validate_declared_competition_effects(struct function_info *function)
{
	struct competition_transfer *transfer = function->competition_transfer;
	struct basic_block *bb;

	if (transfer == NULL)
		return;
	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			enum declared_effect_status status;
			enum locklint_execution_kind kind;
			const char *description;
			int expected;

			if (insn->bb == NULL)
				continue;
			kind = locklint_get_execution_annotation(insn);
			if (kind == LOCKLINT_EXECUTION_NO_COMPETITION_EFFECT) {
				description = "competition-depth decrease";
				expected = -1;
			} else if (kind ==
			    LOCKLINT_EXECUTION_COMPETITION_EFFECT) {
				description = "competition-depth increase";
				expected = 1;
			} else {
				continue;
			}
			status = declared_competition_effect_status(
			    &transfer->output, expected);
			if (status == DECLARED_EFFECT_MATCHES)
				continue;
			if (status == DECLARED_EFFECT_CONDITIONAL) {
				warning(insn->pos, "locklint: declared %s is not "
				    "established on every return from '%s'",
				    description,
				    show_ident(function->ep->name->ident));
			} else {
				warning(insn->pos, "locklint: function '%s' does "
				    "not establish declared %s",
				    show_ident(function->ep->name->ident),
				    description);
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static bool
propagate_transfer_candidates(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct function_info *callee;
			struct lock_transfer *transfer;

			if (insn->bb == NULL)
				continue;
			callee = callgraph_callee(function, insn);
			if (callee == NULL)
				continue;
			for (transfer = callee->transfers; transfer != NULL;
			    transfer = transfer->next) {
				struct acquisition_role role;
				struct locklint_access lock;
				bool added;

				if (!map_acquisition_role(function, insn,
				    &transfer->role, &lock) ||
				    !acquisition_role(function, &lock, &role))
					continue;
				(void) add_transfer(function, &role, &added);
				if (added)
					changed = true;
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
}

static struct transfer_block_info *
find_transfer_block(struct transfer_block_info *blocks,
    struct basic_block *bb)
{
	struct transfer_block_info *block;

	for (block = blocks; block != NULL; block = block->next) {
		if (block->bb == bb)
			return (block);
	}
	return (NULL);
}

static lock_state_t
merge_lock_state(lock_state_t left, lock_state_t right)
{
	return (left | right);
}

static bool
transfer_target_matches(const struct transfer_target *target,
    const struct locklint_access *lock)
{
	const struct mapped_transfer_role *role;

	if (target->single != NULL)
		return (same_lock(target->single, lock));
	for (role = target->roles; role != NULL; role = role->next)
		if (same_lock(&role->transfer->role.access, lock))
			return (true);
	return (false);
}

static void
simulate_instruction(struct function_info *function,
    const struct transfer_target *target, lock_state_t *state,
    unsigned int *invalid, struct instruction *insn)
{
	struct function_info *callee;
	struct lock_transfer *transfer;
	struct locklint_access lock;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;

	if (*state == 0)
		return;
	callee = callgraph_callee(function, insn);
	if (callee != NULL) {
		struct mapped_transfer_role *roles = NULL;
		struct mapped_transfer_role **tail = &roles;
		struct lock_transfer *first = NULL;
		unsigned int role_count = 0;

		for (transfer = callee->transfers; transfer != NULL;
		    transfer = transfer->next) {
			struct mapped_transfer_role *role;

			if (!map_acquisition_role(function, insn,
			    &transfer->role, &lock))
				continue;
			if (!transfer_target_matches(target, &lock))
				continue;
			role_count++;
			if (role_count == 1) {
				first = transfer;
				continue;
			}
			if (role_count == 2) {
				role = calloc(1, sizeof (*role));
				if (role == NULL)
					die("out of memory replaying aliased call");
				role->transfer = first;
				*tail = role;
				tail = &role->next;
			}
			role = calloc(1, sizeof (*role));
			if (role == NULL)
				die("out of memory replaying aliased call");
			role->transfer = transfer;
			*tail = role;
			tail = &role->next;
		}
		if (role_count == 1) {
			*invalid |= first->invalid[*state];
			*state = first->output[*state];
		} else if (role_count > 1) {
			unsigned int nested_invalid;

			*state = simulate_aliased_transfer_context(callee, roles,
			    *state, &nested_invalid, target->replay);
			*invalid |= nested_invalid;
		}
		free_mapped_transfer_roles(roles);
	}

	action = locklint_get_lock_action(function->tu, insn, &lock, &mode);
	if (action == LOCKLINT_LOCK_NONE ||
	    !transfer_target_matches(target, &lock))
		return;
	if (action == LOCKLINT_LOCK_ACQUIRE) {
		if ((*state & LOCK_ANY_HELD) != 0)
			*invalid |= INVALID_ACQUIRE;
		*state = mode;
	} else {
		if ((*state & LOCK_NOT_HELD) != 0)
			*invalid |= INVALID_RELEASE;
		*state = LOCK_NOT_HELD;
	}
}

static void
simulate_block(struct function_info *function,
    struct transfer_block_info *block,
    const struct transfer_target *target, lock_state_t in,
    unsigned int invalid_in, lock_state_t *out,
    unsigned int *invalid_out)
{
	struct instruction *insn;

	*out = in;
	*invalid_out = invalid_in;
	FOR_EACH_PTR(block->bb->insns, insn) {
		if (insn->bb != NULL)
			simulate_instruction(function, target, out, invalid_out,
			    insn);
	} END_FOR_EACH_PTR(insn);
}

static struct transfer_block_info *
solve_transfer_blocks(struct function_info *function,
    const struct transfer_target *target, lock_state_t input)
{
	struct transfer_block_info *blocks = NULL;
	struct transfer_block_info **tail = &blocks;
	struct transfer_block_info *block;
	struct basic_block *bb;
	bool changed;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		block = calloc(1, sizeof (*block));
		if (block == NULL)
			die("out of memory summarizing lock transfer");
		block->bb = bb;
		*tail = block;
		tail = &block->next;
	} END_FOR_EACH_PTR(bb);

	do {
		changed = false;
		for (block = blocks; block != NULL; block = block->next) {
			lock_state_t in = input;
			unsigned int invalid_in = 0;
			struct basic_block *parent;
			bool reachable = block->bb == function->ep->entry->bb;
			bool first = true;
			lock_state_t out;
			unsigned int invalid_out;

			if (!reachable) {
				FOR_EACH_PTR(block->bb->parents, parent) {
					struct transfer_block_info *parent_block;

					parent_block = find_transfer_block(blocks,
					    parent);
					if (parent_block == NULL ||
					    !parent_block->reachable)
						continue;
					if (first) {
						in = parent_block->out;
						invalid_in =
						    parent_block->invalid_out;
						first = false;
					} else {
						in = merge_lock_state(in,
						    parent_block->out);
						invalid_in |=
						    parent_block->invalid_out;
					}
					reachable = true;
				} END_FOR_EACH_PTR(parent);
			}
			if (!reachable)
				continue;
			simulate_block(function, block, target, in, invalid_in,
			    &out, &invalid_out);
			if (!block->reachable || block->in != in ||
			    block->out != out ||
			    block->invalid_in != invalid_in ||
			    block->invalid_out != invalid_out)
				changed = true;
			block->reachable = true;
			block->in = in;
			block->out = out;
			block->invalid_in = invalid_in;
			block->invalid_out = invalid_out;
		}
	} while (changed);
	return (blocks);
}

static void
free_transfer_blocks(struct transfer_block_info *blocks)
{
	while (blocks != NULL) {
		struct transfer_block_info *next = blocks->next;

		free(blocks);
		blocks = next;
	}
}

static lock_state_t
transfer_blocks_result(struct transfer_block_info *blocks,
    lock_state_t input, unsigned int *invalid)
{
	struct transfer_block_info *block;
	lock_state_t result = input;
	unsigned int result_invalid = 0;
	bool found_return = false;

	for (block = blocks; block != NULL; block = block->next) {
		struct instruction *insn;
		bool returns = false;

		if (!block->reachable)
			continue;
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb != NULL && insn->opcode == OP_RET)
				returns = true;
		} END_FOR_EACH_PTR(insn);
		if (!returns || block->out == 0)
			continue;
		if (!found_return) {
			result = block->out;
			result_invalid = block->invalid_out;
			found_return = true;
		} else {
			result = merge_lock_state(result, block->out);
			result_invalid |= block->invalid_out;
		}
	}
	*invalid = result_invalid;
	return (result);
}

/*
 * Simulate one transfer candidate for one possible entry state.  Merge both
 * lock state and invalid-operation flags through the CFG and across all
 * reachable returns.
 */
static lock_state_t
simulate_transfer(struct function_info *function,
    struct lock_transfer *transfer, lock_state_t input,
    unsigned int *invalid)
{
	struct transfer_block_info *blocks;
	struct locklint_access target = { 0 };
	struct transfer_target simulation_target = { 0 };
	lock_state_t result;

	target = transfer->role.access;
	simulation_target.single = &target;
	simulation_target.roles = NULL;
	blocks = solve_transfer_blocks(function, &simulation_target, input);
	result = transfer_blocks_result(blocks, input, invalid);
	free_transfer_blocks(blocks);
	return (result);
}

static bool
same_mapped_transfer_roles(const struct mapped_transfer_role *left,
    const struct mapped_transfer_role *right)
{
	const struct mapped_transfer_role *role;
	unsigned int left_count = 0;
	unsigned int right_count = 0;

	for (role = left; role != NULL; role = role->next) {
		const struct mapped_transfer_role *other;

		left_count++;
		for (other = right; other != NULL; other = other->next) {
			if (other->transfer == role->transfer)
				break;
		}
		if (other == NULL)
			return (false);
	}
	for (role = right; role != NULL; role = role->next)
		right_count++;
	return (left_count == right_count);
}

/*
 * Replay an aliased transfer group as one shared lock state.  Recursive
 * contexts iterate from the empty result to the least fixed point.  Frames
 * installed by prefix replay have no approximation and retain the
 * conservative recursion fallback.
 */
static lock_state_t
simulate_aliased_transfer_context(struct function_info *function,
    const struct mapped_transfer_role *roles, lock_state_t input,
    unsigned int *invalid, const struct alias_replay_frame *replay)
{
	const struct alias_replay_frame *ancestor;
	struct alias_replay_frame frame = { 0 };
	struct transfer_target target = { 0 };

	for (ancestor = replay; ancestor != NULL; ancestor = ancestor->next) {
		if (ancestor->function != function)
			continue;
		if (ancestor->iterative && ancestor->input == input &&
		    same_mapped_transfer_roles(ancestor->roles, roles)) {
			*invalid = ancestor->invalid;
			return (ancestor->output);
		}
		if (!ancestor->iterative) {
			*invalid = INVALID_ACQUIRE | INVALID_RELEASE;
			return (LOCK_NOT_HELD | LOCK_ANY_HELD);
		}
	}
	frame.function = function;
	frame.roles = roles;
	frame.input = input;
	frame.iterative = true;
	frame.next = replay;
	target.roles = roles;
	target.replay = &frame;
	for (;;) {
		struct transfer_block_info *blocks;
		lock_state_t output;
		unsigned int iteration_invalid;

		blocks = solve_transfer_blocks(function, &target, input);
		output = transfer_blocks_result(blocks, input,
		    &iteration_invalid);
		free_transfer_blocks(blocks);
		output |= frame.output;
		iteration_invalid |= frame.invalid;
		if (output == frame.output &&
		    iteration_invalid == frame.invalid)
			break;
		frame.output = output;
		frame.invalid = iteration_invalid;
	}
	*invalid = frame.invalid;
	return (frame.output);
}

static lock_state_t
simulate_aliased_transfer(struct function_info *function,
    const struct mapped_transfer_role *roles, lock_state_t input,
    unsigned int *invalid)
{
	return (simulate_aliased_transfer_context(function, roles, input,
	    invalid, NULL));
}

static lock_state_t
simulate_lock_prefix_target(struct function_info *function,
    const struct transfer_target *target, lock_state_t input,
    struct instruction *checkpoint, bool *reachable)
{
	struct transfer_block_info *blocks;
	struct transfer_block_info *block;
	lock_state_t state = input;
	unsigned int invalid = 0;

	blocks = solve_transfer_blocks(function, target, input);
	block = find_transfer_block(blocks, checkpoint->bb);
	if (block == NULL || !block->reachable) {
		if (reachable != NULL)
			*reachable = false;
		free_transfer_blocks(blocks);
		return (input);
	}
	if (reachable != NULL)
		*reachable = true;
	state = block->in;
	invalid = block->invalid_in;
	{
		struct instruction *insn;

		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn == checkpoint)
				goto prefix_done;
			if (insn->bb != NULL)
				simulate_instruction(function, target,
				    &state, &invalid, insn);
		} END_FOR_EACH_PTR(insn);
	}
prefix_done:
	free_transfer_blocks(blocks);
	return (state);
}

static lock_state_t
simulate_lock_prefix(struct function_info *function,
    const struct acquisition_role *role, lock_state_t input,
    struct instruction *checkpoint, bool *reachable)
{
	struct transfer_target target = { 0 };

	target.single = &role->access;
	return (simulate_lock_prefix_target(function, &target, input,
	    checkpoint, reachable));
}

static bool
solve_function_transfers(struct function_info *function)
{
	struct lock_transfer *transfer;
	bool changed = false;

	for (transfer = function->transfers; transfer != NULL;
	    transfer = transfer->next) {
		unsigned int input;

		for (input = 1; input < LOCK_STATE_COUNT; input++) {
			lock_state_t output;
			unsigned int invalid;

			output = simulate_transfer(function, transfer, input,
			    &invalid);
			if (transfer->output[input] != output ||
			    transfer->invalid[input] != invalid) {
				transfer->output[input] = output;
				transfer->invalid[input] = invalid;
				changed = true;
			}
		}
	}
	return (changed);
}

static bool
same_requirement_origin(const struct assertion_requirement *requirement,
    struct translation_unit *origin_tu, struct position pos)
{
	return (requirement->origin_tu == origin_tu &&
	    requirement->pos.stream == pos.stream &&
	    requirement->pos.line == pos.line &&
	    requirement->pos.pos == pos.pos);
}

static bool
same_assertion_alternative_roles(
    const struct assertion_alternative_role *left,
    const struct assertion_alternative_role *right)
{
	const struct assertion_alternative_role *role;
	unsigned int left_count = 0;
	unsigned int right_count = 0;

	for (role = left; role != NULL; role = role->next) {
		const struct assertion_alternative_role *other;

		left_count++;
		for (other = right; other != NULL; other = other->next) {
			if (same_acquisition_role(&role->role, &other->role))
				break;
		}
		if (other == NULL)
			return (false);
	}
	for (role = right; role != NULL; role = role->next)
		right_count++;
	return (left_count == right_count);
}

static const struct assertion_alternative *
find_assertion_alternative(const struct assertion_alternative *alternatives,
    const struct assertion_alternative_role *roles)
{
	for (; alternatives != NULL; alternatives = alternatives->next) {
		if (same_assertion_alternative_roles(alternatives->roles, roles))
			return (alternatives);
	}
	return (NULL);
}

static bool
add_assertion_alternative_role(struct assertion_alternative_role **roles,
    const struct acquisition_role *role)
{
	struct assertion_alternative_role **tail;

	for (tail = roles; *tail != NULL; tail = &(*tail)->next) {
		if (same_acquisition_role(&(*tail)->role, role))
			return (false);
	}
	*tail = calloc(1, sizeof (**tail));
	if (*tail == NULL)
		die("out of memory recording assertion alias role");
	(*tail)->role = *role;
	return (true);
}

static void
free_assertion_alternative_roles(struct assertion_alternative_role *roles)
{
	while (roles != NULL) {
		struct assertion_alternative_role *next = roles->next;

		free(roles);
		roles = next;
	}
}

static bool
add_assertion_alternative(struct assertion_alternative **alternatives,
    const struct assertion_alternative_role *roles,
    unsigned int accepted_inputs)
{
	struct assertion_alternative *alternative;
	const struct assertion_alternative_role *role;

	for (alternative = *alternatives; alternative != NULL;
	    alternative = alternative->next) {
		if (same_assertion_alternative_roles(alternative->roles, roles))
			break;
	}
	if (alternative != NULL) {
		unsigned int merged =
		    alternative->accepted_inputs & accepted_inputs;

		if (merged == alternative->accepted_inputs)
			return (false);
		alternative->accepted_inputs = merged;
		return (true);
	}
	alternative = calloc(1, sizeof (*alternative));
	if (alternative == NULL)
		die("out of memory recording assertion alias alternative");
	for (role = roles; role != NULL; role = role->next)
		(void) add_assertion_alternative_role(&alternative->roles,
		    &role->role);
	alternative->accepted_inputs = accepted_inputs;
	alternative->next = *alternatives;
	*alternatives = alternative;
	return (true);
}

static void
free_assertion_alternatives(struct assertion_alternative *alternatives)
{
	while (alternatives != NULL) {
		struct assertion_alternative *next = alternatives->next;

		free_assertion_alternative_roles(alternatives->roles);
		free(alternatives);
		alternatives = next;
	}
}

/*
 * Merge alternatives from another path.  A missing alternative on either
 * path contributes that path's ordinary accepted-input mask.
 */
static bool
merge_assertion_alternatives(struct assertion_requirement *requirement,
    const struct assertion_alternative *incoming, unsigned int old_base,
    unsigned int incoming_base)
{
	struct assertion_alternative *alternative;
	const struct assertion_alternative *other;
	bool changed = false;

	for (alternative = requirement->alternatives; alternative != NULL;
	    alternative = alternative->next) {
		unsigned int accepted_inputs;

		other = find_assertion_alternative(incoming,
		    alternative->roles);
		accepted_inputs = alternative->accepted_inputs &
		    (other != NULL ? other->accepted_inputs : incoming_base);
		if (accepted_inputs != alternative->accepted_inputs) {
			alternative->accepted_inputs = accepted_inputs;
			changed = true;
		}
	}
	for (other = incoming; other != NULL; other = other->next) {
		if (find_assertion_alternative(requirement->alternatives,
		    other->roles) != NULL)
			continue;
		if (add_assertion_alternative(&requirement->alternatives,
		    other->roles, other->accepted_inputs & old_base))
			changed = true;
	}
	return (changed);
}

static bool
add_assertion_requirement(struct function_info *function,
    const struct acquisition_role *role, unsigned int modes,
    unsigned int accepted_inputs, struct translation_unit *origin_tu,
    struct position pos, struct function_info *source_function,
    struct instruction *checkpoint,
    const struct assertion_alternative *alternatives)
{
	struct assertion_requirement **link;
	const struct assertion_alternative *alternative;
	bool meaningful = false;

	for (alternative = alternatives; alternative != NULL;
	    alternative = alternative->next) {
		if (alternative->accepted_inputs != accepted_inputs) {
			meaningful = true;
			break;
		}
	}
	if (accepted_inputs == ALL_LOCK_INPUTS && !meaningful)
		return (false);
	for (link = &function->assertion_requirements; *link != NULL;
	    link = &(*link)->next) {
		struct assertion_requirement *requirement = *link;
		unsigned int old_base;
		unsigned int merged;

		if (requirement->modes != modes ||
		    !same_acquisition_role(&requirement->role, role) ||
		    !same_requirement_origin(requirement, origin_tu, pos))
			continue;
		old_base = requirement->accepted_inputs;
		merged = requirement->accepted_inputs & accepted_inputs;
		if (merged != requirement->accepted_inputs)
			requirement->accepted_inputs = merged;
		return (merge_assertion_alternatives(requirement,
		    alternatives, old_base, accepted_inputs) ||
		    merged != old_base);
	}
	*link = calloc(1, sizeof (**link));
	if (*link == NULL)
		die("out of memory recording assertion requirement");
	(*link)->role = *role;
	(*link)->modes = modes;
	(*link)->accepted_inputs = accepted_inputs;
	(*link)->source_function = source_function;
	(*link)->checkpoint = checkpoint;
	(*link)->origin_tu = origin_tu;
	(*link)->pos = pos;
	for (; alternatives != NULL; alternatives = alternatives->next) {
		(void) add_assertion_alternative(&(*link)->alternatives,
		    alternatives->roles, alternatives->accepted_inputs);
	}
	return (true);
}

static unsigned int
local_assertion_alias_inputs(struct function_info *function,
    const struct acquisition_role *asserted_role,
    const struct assertion_alternative_role *roles, unsigned int modes,
    struct instruction *checkpoint, bool *reachable)
{
	const struct assertion_alternative_role *role;
	struct mapped_transfer_role *mapped;
	struct lock_transfer *transfers;
	struct alias_replay_frame frame = { 0 };
	struct transfer_target target = { 0 };
	unsigned int accepted_inputs = 0;
	unsigned int count = 1;
	unsigned int index = 0;
	unsigned int input;

	for (role = roles; role != NULL; role = role->next)
		count++;
	transfers = calloc(count, sizeof (*transfers));
	mapped = calloc(count, sizeof (*mapped));
	if (transfers == NULL || mapped == NULL)
		die("out of memory replaying assertion alias roles");
	transfers[index].role = *asserted_role;
	mapped[index].transfer = &transfers[index];
	index++;
	for (role = roles; role != NULL; role = role->next) {
		transfers[index].role = role->role;
		mapped[index].transfer = &transfers[index];
		mapped[index - 1].next = &mapped[index];
		index++;
	}
	frame.function = function;
	target.roles = mapped;
	target.replay = &frame;
	*reachable = false;
	for (input = 1; input < LOCK_STATE_COUNT; input++) {
		lock_state_t state;

		state = simulate_lock_prefix_target(function, &target, input,
		    checkpoint, reachable);
		if ((state & ~modes) == 0)
			accepted_inputs |= 1U << input;
	}
	free(mapped);
	free(transfers);
	return (accepted_inputs);
}

struct assertion_subset_context {
	struct function_info *function;
	const struct acquisition_role *asserted_role;
	unsigned int modes;
	unsigned int ordinary_inputs;
	struct instruction *checkpoint;
	struct assertion_alternative **alternatives;
	bool meaningful;
};

/*
 * Retain the accepted-input table for every nonempty subset of other transfer
 * roles.  Exact simultaneous aliases can require a table even when a smaller
 * subset happens to have the ordinary result.
 */
static void
collect_assertion_subsets(struct assertion_subset_context *context,
    const struct assertion_alternative_role *candidate,
    struct assertion_alternative_role *selected)
{
	if (candidate == NULL) {
		unsigned int accepted_inputs;
		bool reachable;

		if (selected == NULL)
			return;
		accepted_inputs = local_assertion_alias_inputs(
		    context->function, context->asserted_role, selected,
		    context->modes, context->checkpoint, &reachable);
		if (!reachable)
			return;
		(void) add_assertion_alternative(context->alternatives,
		    selected, accepted_inputs);
		if (accepted_inputs != context->ordinary_inputs)
			context->meaningful = true;
		return;
	}
	collect_assertion_subsets(context, candidate->next, selected);
	{
		struct assertion_alternative_role included = {
			.role = candidate->role,
			.next = selected
		};

		collect_assertion_subsets(context, candidate->next, &included);
	}
}

/*
 * Record how every subset of other transfer roles changes the assertion when
 * that subset denotes the asserted lock.  The complete subset key preserves
 * combinations for exact selection after mapping through wrappers.
 */
static struct assertion_alternative *
collect_assertion_alternatives(struct function_info *function,
    const struct acquisition_role *asserted_role, unsigned int modes,
    unsigned int ordinary_inputs, struct instruction *checkpoint)
{
	struct assertion_alternative_role *candidates = NULL;
	struct assertion_alternative *alternatives = NULL;
	struct assertion_subset_context context = {
		.function = function,
		.asserted_role = asserted_role,
		.modes = modes,
		.ordinary_inputs = ordinary_inputs,
		.checkpoint = checkpoint,
		.alternatives = &alternatives
	};
	struct lock_transfer *transfer;

	for (transfer = function->transfers; transfer != NULL;
	    transfer = transfer->next) {
		if (!same_acquisition_role(&transfer->role, asserted_role))
			(void) add_assertion_alternative_role(&candidates,
			    &transfer->role);
	}
	collect_assertion_subsets(&context, candidates, NULL);
	free_assertion_alternative_roles(candidates);
	if (!context.meaningful) {
		free_assertion_alternatives(alternatives);
		alternatives = NULL;
	}
	return (alternatives);
}

static void
collect_assertion_requirements(struct function_info *function)
{
	struct basic_block *bb;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct acquisition_role role;
			struct locklint_access lock;
			struct position pos;
			unsigned int accepted_inputs = 0;
			struct assertion_alternative *alternatives;
			bool reachable = false;
			unsigned int input;
			unsigned int modes;

			if (insn->bb == NULL ||
			    !locklint_get_assertion(function->tu, insn, &lock,
			    &modes) ||
			    !acquisition_role(function, &lock, &role))
				continue;
			pos = insn->call_expr != NULL ?
			    insn->call_expr->pos : insn->pos;
			for (input = 1; input < LOCK_STATE_COUNT; input++) {
				lock_state_t state;

				state = simulate_lock_prefix(function, &role,
				    input, insn, &reachable);
				if ((state & ~modes) == 0)
					accepted_inputs |= 1U << input;
			}
			if (!reachable)
				continue;
			alternatives = collect_assertion_alternatives(function,
			    &role, modes, accepted_inputs, insn);
			(void) add_assertion_requirement(function, &role, modes,
			    accepted_inputs, function->tu, pos, function, insn,
			    alternatives);
			free_assertion_alternatives(alternatives);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static bool
add_acquisition_candidate(struct acquisition_candidate **candidates,
    const struct acquisition_role *role)
{
	struct acquisition_candidate **tail;

	for (tail = candidates; *tail != NULL; tail = &(*tail)->next) {
		if (same_acquisition_role(&(*tail)->role, role))
			return (false);
	}
	*tail = calloc(1, sizeof (**tail));
	if (*tail == NULL)
		die("out of memory collecting acquisition summary roles");
	(*tail)->role = *role;
	return (true);
}

static void
collect_local_acquisition_roles(struct function_info *function)
{
	struct basic_block *bb;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct locklint_access lock;
			struct acquisition_role role;
			enum locklint_lock_action action;
			enum locklint_lock_mode mode;

			if (insn->bb == NULL ||
			    !acquisition_block_reachable(function, insn->bb))
				continue;
			action = locklint_get_lock_action(function->tu, insn,
			    &lock, &mode);
			if (action != LOCKLINT_LOCK_NONE &&
			    lock.root != NULL &&
			    acquisition_role(function, &lock, &role))
				(void) add_acquisition_candidate(
				    &function->acquisition_roles, &role);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static bool
acquisition_block_reachable(struct function_info *function,
    struct basic_block *bb)
{
	struct block_info *block;

	for (block = function->blocks; block != NULL; block = block->next) {
		if (block->bb == bb)
			return (block->reachable);
	}
	return (false);
}

static bool
propagate_acquisition_roles(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct function_info *callee;
			struct acquisition_candidate *candidate;

			if (insn->bb == NULL ||
			    !acquisition_block_reachable(function, insn->bb))
				continue;
			callee = callgraph_callee(function, insn);
			if (callee == NULL)
				continue;
			for (candidate = callee->acquisition_roles;
			    candidate != NULL; candidate = candidate->next) {
				struct locklint_access mapped;
				struct acquisition_role role;

				if (!map_acquisition_role(function, insn,
				    &candidate->role, &mapped) ||
				    !acquisition_role(function, &mapped, &role))
					continue;
				if (add_acquisition_candidate(
				    &function->acquisition_roles, &role))
					changed = true;
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
}

static void
free_acquisition_candidates(struct acquisition_candidate *candidates)
{
	while (candidates != NULL) {
		struct acquisition_candidate *next = candidates->next;

		free(candidates);
		candidates = next;
	}
}

static void
free_acquisition_prefixes(struct acquisition_prefix *prefixes)
{
	while (prefixes != NULL) {
		struct acquisition_prefix *next = prefixes->next;

		free(prefixes);
		prefixes = next;
	}
}

static bool
apply_callee_acquisition_prefix(struct function_info *function,
    struct instruction *insn,
    const struct acquisition_summary *callee_summary,
    const struct acquisition_role *role, lock_state_t *state)
{
	const struct acquisition_prefix *prefix;
	unsigned int matches = 0;
	lock_state_t output = 0;

	for (prefix = callee_summary->prefixes; prefix != NULL;
	    prefix = prefix->next) {
		struct locklint_access mapped;

		if (!map_acquisition_role(function, insn, &prefix->role,
		    &mapped) || !same_lock(&role->access, &mapped))
			continue;
		if (prefix->unknown)
			return (false);
		output |= prefix->output[*state];
		matches++;
	}
	if (matches > 1)
		return (false);
	if (matches == 1)
		*state = output;
	return (true);
}

static void
summarize_acquisition_prefix(struct function_info *function,
    struct acquisition_candidate *candidates, struct instruction *insn,
    const struct acquisition_summary *callee_summary,
    struct acquisition_summary *summary)
{
	struct acquisition_candidate *candidate;
	struct acquisition_prefix **tail = &summary->prefixes;

	for (candidate = candidates; candidate != NULL;
	    candidate = candidate->next) {
		struct acquisition_prefix *prefix;
		unsigned int input;
		bool changed = false;

		prefix = calloc(1, sizeof (*prefix));
		if (prefix == NULL)
			die("out of memory summarizing acquisition");
		prefix->role = candidate->role;
		for (input = 1; input < LOCK_STATE_COUNT; input++) {
			lock_state_t state;

			state = simulate_lock_prefix(function,
			    &candidate->role, input, insn, NULL);
			if (callee_summary != NULL &&
			    !apply_callee_acquisition_prefix(function, insn,
			    callee_summary, &candidate->role,
			    &state)) {
				prefix->unknown = true;
				state = input;
			}
			prefix->output[input] = state;
			if (prefix->unknown || state != input)
				changed = true;
		}
		if (!changed) {
			free(prefix);
			continue;
		}
		*tail = prefix;
		tail = &prefix->next;
	}
}

static const struct acquisition_prefix *
find_acquisition_prefix(const struct acquisition_summary *summary,
    const struct acquisition_role *role)
{
	const struct acquisition_prefix *prefix;

	for (prefix = summary->prefixes; prefix != NULL;
	    prefix = prefix->next) {
		if (same_acquisition_role(&prefix->role, role))
			return (prefix);
	}
	return (NULL);
}

static bool
same_acquisition_summary(const struct acquisition_summary *left,
    const struct acquisition_summary *right)
{
	const struct acquisition_prefix *prefix;
	unsigned int left_count = 0;
	unsigned int right_count = 0;

	if (!same_acquisition_role(&left->acquired, &right->acquired))
		return (false);
	if (left->source_function != right->source_function ||
	    left->checkpoint != right->checkpoint)
		return (false);
	for (prefix = left->prefixes; prefix != NULL; prefix = prefix->next) {
		const struct acquisition_prefix *other;

		left_count++;
		other = find_acquisition_prefix(right, &prefix->role);
		if (other == NULL ||
		    prefix->unknown != other->unknown ||
		    memcmp(prefix->output, other->output,
		    sizeof (prefix->output)) != 0)
			return (false);
	}
	for (prefix = right->prefixes; prefix != NULL; prefix = prefix->next)
		right_count++;
	return (left_count == right_count);
}

static bool
add_acquisition_summary(struct function_info *function,
    struct acquisition_summary *summary)
{
	struct acquisition_summary **tail;

	for (tail = &function->acquisitions; *tail != NULL;
	    tail = &(*tail)->next) {
		if (!same_acquisition_summary(*tail, summary))
			continue;
		free_acquisition_prefixes(summary->prefixes);
		free(summary);
		return (false);
	}
	*tail = summary;
	return (true);
}

static void
collect_local_acquisition_summaries(struct function_info *function)
{
	struct basic_block *bb;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct locklint_access lock;
			struct acquisition_role acquired;
			struct acquisition_summary *summary;
			enum locklint_lock_action action;
			enum locklint_lock_mode mode;

			if (insn->bb == NULL ||
			    !acquisition_block_reachable(function, insn->bb))
				continue;
			action = locklint_get_lock_action(function->tu, insn,
			    &lock, &mode);
			if (action != LOCKLINT_LOCK_ACQUIRE ||
			    lock.root == NULL ||
			    !acquisition_role(function, &lock, &acquired))
				continue;
			summary = calloc(1, sizeof (*summary));
			if (summary == NULL)
				die("out of memory collecting acquisitions");
			summary->acquired = acquired;
			summary->pos = insn->call_expr != NULL ?
			    insn->call_expr->pos : insn->pos;
			summary->source_function = function;
			summary->checkpoint = insn;
			summarize_acquisition_prefix(function,
			    function->acquisition_roles, insn, NULL,
			    summary);
			(void) add_acquisition_summary(function, summary);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static bool
propagate_acquisition_summaries(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct function_info *callee;
			struct acquisition_summary *summary;
			struct acquisition_summary *last;

			if (insn->bb == NULL ||
			    !acquisition_block_reachable(function, insn->bb))
				continue;
			callee = callgraph_callee(function, insn);
			if (callee == NULL || callee->acquisitions == NULL)
				continue;
			last = callee->acquisitions;
			while (last->next != NULL)
				last = last->next;
			for (summary = callee->acquisitions; summary != NULL;
			    summary = summary->next) {
				struct locklint_access mapped;
				struct acquisition_role acquired;
				struct acquisition_summary *propagated;
				bool at_last = summary == last;

				if (!map_acquisition_role(function, insn,
				    &summary->acquired, &mapped) ||
				    !acquisition_role(function, &mapped,
				    &acquired))
					goto next_summary;
				propagated = calloc(1, sizeof (*propagated));
				if (propagated == NULL)
					die("out of memory propagating acquisition");
				propagated->acquired = acquired;
				propagated->pos = summary->pos;
				propagated->source_function =
				    summary->source_function;
				propagated->checkpoint = summary->checkpoint;
				summarize_acquisition_prefix(function,
				    function->acquisition_roles, insn, summary,
				    propagated);
				if (add_acquisition_summary(function, propagated))
					changed = true;
next_summary:
				if (at_last)
					break;
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
}

static void
collect_visibility_transfer_expression(struct function_info *function,
    struct expression *expr, bool *changed)
{
	struct locklint_access access;
	struct symbol *data_root;
	struct object_identity *data_object;
	unsigned int argument;
	bool added;

	if (expr == NULL)
		return;
	if (expr->type == EXPR_COMMA) {
		collect_visibility_transfer_expression(function, expr->left,
		    changed);
		collect_visibility_transfer_expression(function, expr->right,
		    changed);
		return;
	}
	if (!locklint_get_access(function->tu, expr, &access) ||
	    !condition_data_identity(function, &access, &argument, &data_root,
	    &data_object))
		return;
	(void) add_visibility_transfer(function, argument, data_root,
	    data_object, access.member, access.offset, access.path, &added);
	*changed |= added;
}

static bool
collect_local_visibility_transfers(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			enum locklint_execution_kind kind;

			if (insn->bb == NULL)
				continue;
			kind = locklint_get_execution_annotation(insn);
			if (kind != LOCKLINT_EXECUTION_INVISIBLE &&
			    kind != LOCKLINT_EXECUTION_VISIBLE)
				continue;
			collect_visibility_transfer_expression(function,
			    insn->context_expr, &changed);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
}

static bool
propagate_visibility_transfer_candidates(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct function_info *callee;
			struct visibility_transfer *transfer;

			if (insn->bb == NULL)
				continue;
			callee = callgraph_callee(function, insn);
			if (callee == NULL)
				continue;
			for (transfer = callee->visibility_transfers;
			    transfer != NULL; transfer = transfer->next) {
				struct locklint_access access;
				struct symbol *data_root;
				struct object_identity *data_object;
				unsigned int argument;
				bool added;

				if (!map_call_visibility_transfer(function, insn,
				    transfer, &access) ||
				    !condition_data_identity(function, &access,
				    &argument, &data_root, &data_object))
					continue;
				(void) add_visibility_transfer(function, argument,
				    data_root, data_object, access.member,
				    access.offset, access.path, &added);
				changed |= added;
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
}

static struct visibility_transfer_block_info *
find_visibility_transfer_block(struct visibility_transfer_block_info *blocks,
    struct basic_block *bb)
{
	struct visibility_transfer_block_info *block;

	for (block = blocks; block != NULL; block = block->next) {
		if (block->bb == bb)
			return (block);
	}
	return (NULL);
}

static void
simulate_visibility_expression(struct function_info *function,
    struct locklint_access *target, struct expression *expr,
    enum visibility_state assigned, enum visibility_state *state)
{
	struct locklint_access region;

	if (expr == NULL)
		return;
	if (expr->type == EXPR_COMMA) {
		simulate_visibility_expression(function, target, expr->left,
		    assigned, state);
		simulate_visibility_expression(function, target, expr->right,
		    assigned, state);
		return;
	}
	if (locklint_get_access(function->tu, expr, &region) &&
	    locklint_access_contains(&region, target))
		*state = assigned;
}

static void
simulate_visibility_instruction(struct function_info *function,
    struct locklint_access *target, enum visibility_state *state,
    struct instruction *insn)
{
	struct function_info *callee;
	struct mapped_visibility_effect *effects;
	struct mapped_visibility_effect *effect;

	callee = callgraph_callee(function, insn);
	if (callee != NULL) {
		struct mapped_visibility_effect *selected = NULL;

		effects = map_call_visibility_effects(function, insn, callee);
		for (effect = effects; effect != NULL; effect = effect->next) {
			if (locklint_access_contains(&effect->region, target))
				selected = effect;
		}
		if (selected != NULL)
			*state = selected->output[*state];
		free_mapped_visibility_effects(effects);
	}
	switch (locklint_get_execution_annotation(insn)) {
	case LOCKLINT_EXECUTION_INVISIBLE:
		simulate_visibility_expression(function, target,
		    insn->context_expr, VISIBILITY_INVISIBLE, state);
		break;
	case LOCKLINT_EXECUTION_VISIBLE:
		simulate_visibility_expression(function, target,
		    insn->context_expr, VISIBILITY_VISIBLE, state);
		break;
	default:
		break;
	}
}

static enum visibility_state
simulate_visibility_transfer(struct function_info *function,
    struct visibility_transfer *transfer, enum visibility_state input)
{
	struct visibility_transfer_block_info *blocks = NULL;
	struct visibility_transfer_block_info **tail = &blocks;
	struct visibility_transfer_block_info *block;
	struct basic_block *bb;
	struct locklint_access target;
	enum visibility_state result = input;
	bool found_return = false;
	bool changed;

	visibility_transfer_access(function, transfer, &target);
	FOR_EACH_PTR(function->ep->bbs, bb) {
		block = calloc(1, sizeof (*block));
		if (block == NULL)
			die("out of memory summarizing visibility transfer");
		block->bb = bb;
		*tail = block;
		tail = &block->next;
	} END_FOR_EACH_PTR(bb);

	do {
		changed = false;
		for (block = blocks; block != NULL; block = block->next) {
			enum visibility_state in = input;
			enum visibility_state out;
			struct basic_block *parent;
			bool reachable = block->bb == function->ep->entry->bb;
			bool first = true;
			struct instruction *insn;

			if (!reachable) {
				FOR_EACH_PTR(block->bb->parents, parent) {
					struct visibility_transfer_block_info
					    *parent_block;

					parent_block =
					    find_visibility_transfer_block(blocks,
					    parent);
					if (parent_block == NULL ||
					    !parent_block->reachable)
						continue;
					if (first) {
						in = parent_block->out;
						first = false;
					} else {
						in = merge_visibility_state(in,
						    parent_block->out);
					}
					reachable = true;
				} END_FOR_EACH_PTR(parent);
			}
			if (!reachable)
				continue;
			out = in;
			FOR_EACH_PTR(block->bb->insns, insn) {
				if (insn->bb != NULL) {
					simulate_visibility_instruction(function,
					    &target, &out, insn);
				}
			} END_FOR_EACH_PTR(insn);
			if (!block->reachable || block->in != in ||
			    block->out != out)
				changed = true;
			block->reachable = true;
			block->in = in;
			block->out = out;
		}
	} while (changed);

	for (block = blocks; block != NULL; block = block->next) {
		struct instruction *insn;
		bool returns = false;

		if (!block->reachable)
			continue;
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb != NULL && insn->opcode == OP_RET)
				returns = true;
		} END_FOR_EACH_PTR(insn);
		if (!returns)
			continue;
		if (!found_return) {
			result = block->out;
			found_return = true;
		} else {
			result = merge_visibility_state(result, block->out);
		}
	}
	while (blocks != NULL) {
		struct visibility_transfer_block_info *next = blocks->next;

		free(blocks);
		blocks = next;
	}
	return (result);
}

static bool
solve_function_visibility_transfers(struct function_info *function)
{
	struct visibility_transfer *transfer;
	bool changed = false;

	for (transfer = function->visibility_transfers; transfer != NULL;
	    transfer = transfer->next) {
		unsigned int input;

		for (input = 0; input < VISIBILITY_STATE_COUNT; input++) {
			enum visibility_state output;

			output = simulate_visibility_transfer(function, transfer,
			    input);
			if (transfer->output[input] != output) {
				transfer->output[input] = output;
				changed = true;
			}
		}
	}
	return (changed);
}

static void
record_assumption_expression(struct function_info *function,
    struct expression *expr)
{
	struct locklint_access access;
	struct assumed_region *assumption;

	if (expr == NULL)
		return;
	if (expr->type == EXPR_COMMA) {
		record_assumption_expression(function, expr->left);
		record_assumption_expression(function, expr->right);
		return;
	}
	if (!locklint_get_access(function->tu, expr, &access))
		return;
	for (assumption = function->assumptions; assumption != NULL;
	    assumption = assumption->next) {
		if (locklint_same_access(&assumption->region, &access))
			return;
	}
	assumption = calloc(1, sizeof (*assumption));
	if (assumption == NULL)
		die("out of memory recording assumed protection");
	assumption->region = access;
	assumption->pos = expr->pos;
	assumption->next = function->assumptions;
	function->assumptions = assumption;
}

static void
collect_function_assumptions(struct function_info *function)
{
	struct basic_block *bb;

	/* The retained marker has a location, but its contract starts at entry. */
	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			if (insn->bb == NULL ||
			    locklint_get_execution_annotation(insn) !=
			    LOCKLINT_EXECUTION_ASSUME_PROTECTED)
				continue;
			record_assumption_expression(function,
			    insn->context_expr);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static void
collect_assumption_conditions(struct function_info *function)
{
	struct assumed_region *assumption;

	for (assumption = function->assumptions; assumption != NULL;
	    assumption = assumption->next) {
		struct locklint_access *access = &assumption->region;
		struct locklint_access lock = { 0 };
		struct locklint_data_policy policy;
		struct symbol *data_root;
		struct object_identity *data_object;
		unsigned int argument;
		unsigned int required_modes = 0;
		bool has_lock;

		if (!condition_data_identity(function, access, &argument,
		    &data_root, &data_object))
			continue;
		has_lock = locklint_data_policy(access, &policy, &lock) &&
		    (policy.protection == LOCKLINT_PROTECTION_MUTEX ||
		    policy.protection == LOCKLINT_PROTECTION_RWLOCK);
		if (policy.protection == LOCKLINT_PROTECTION_MUTEX)
			required_modes = LOCK_HELD;
		else if (policy.protection == LOCKLINT_PROTECTION_RWLOCK)
			required_modes = LOCK_WRITE_HELD;
		(void) add_protection_condition(function, argument,
		    data_root, data_object, has_lock, required_modes,
		    has_lock && lock.root != access->root ? lock.root : NULL,
		    has_lock && lock.root != access->root ? lock.object : NULL,
		    has_lock ? lock.member : NULL, has_lock ? lock.offset : 0,
		    access->member, access->offset, assumption->pos, NULL);
	}
}

static void
diagnose_assumption_expression(struct function_info *function,
    struct expression *expr)
{
	struct locklint_access access;

	if (expr == NULL)
		return;
	if (expr->type == EXPR_COMMA) {
		diagnose_assumption_expression(function, expr->left);
		diagnose_assumption_expression(function, expr->right);
		return;
	}
	if (!locklint_get_access(function->tu, expr, &access)) {
		warning(expr->pos,
		    "locklint: ASSUMING_PROTECTED has no object");
	}
}

static void
diagnose_assumption(struct function_info *function,
    const struct instruction *insn)
{
	if (locklint_get_execution_annotation(insn) !=
	    LOCKLINT_EXECUTION_ASSUME_PROTECTED)
		return;
	diagnose_assumption_expression(function, insn->context_expr);
}

static void
collect_local_protection_conditions(struct function_info *function)
{
	struct block_info *block;

	collect_assumption_conditions(function);
	for (block = function->blocks; block != NULL; block = block->next) {
		struct analysis_state *state;
		struct instruction *insn;

		if (!block->reachable)
			continue;
		state = copy_analysis_state(block->in);
		FOR_EACH_PTR(block->bb->insns, insn) {
			struct locklint_access access;
			struct locklint_access lock;
			struct symbol *data_member;
			struct symbol *data_root;
			struct object_identity *data_object;
			unsigned int argument;
			unsigned int required_modes;

			if (insn->bb == NULL)
				continue;
			if (protected_access(function, insn, &access, &lock,
			    &data_member, &required_modes) &&
			    get_protection_status(function, state, &access,
			    required_modes, &lock) == PROTECTION_ABSENT &&
			    condition_data_identity(function, &access, &argument,
			    &data_root, &data_object)) {
				struct protection_alternative *alternatives;

				alternatives =
				    collect_state_protection_alternatives(
				    function, required_modes, state);
				(void) add_protection_condition(function,
				    argument,
				    data_root, data_object,
				    true, required_modes,
				    lock.root != access.root ? lock.root : NULL,
				    lock.root != access.root ?
				    lock.object : NULL,
				    lock.member, lock.offset, data_member,
				    access.offset, insn->access->pos,
				    alternatives);
				free_protection_alternatives(alternatives);
			}
			transfer_instruction(function, state, insn);
		} END_FOR_EACH_PTR(insn);
		free_analysis_state(state);
	}
}

static bool
propagate_call_protection_conditions(struct function_info *function,
    struct analysis_state *state, struct instruction *insn)
{
	struct function_info *callee = callgraph_callee(function, insn);
	struct protection_condition *condition;
	bool changed = false;

	if (callee == NULL)
		return (false);
	for (condition = callee->conditions; condition != NULL;
	    condition = condition->next) {
		struct locklint_access object;
		struct locklint_access lock;
		struct symbol *data_root;
		struct object_identity *data_object;
		enum protection_status status;
		unsigned int caller_argument;
		struct protection_alternative *alternative;
		struct protection_alternative *alternatives = NULL;

		if (!map_call_protection_condition(function, insn, condition,
		    &object, &lock))
			continue;
		if (condition_alternative_satisfied(function, insn, condition,
		    &lock))
			continue;
		status = get_protection_status(function, state, &object,
		    condition->required_modes, &lock);
		if (status != PROTECTION_ABSENT ||
		    !condition_data_identity(function, &object,
		    &caller_argument, &data_root, &data_object))
			continue;
		for (alternative = condition->alternatives;
		    alternative != NULL; alternative = alternative->next) {
			struct locklint_access candidate;
			unsigned int argument;

			if (!argument_lock(function->tu, insn,
			    alternative->argument,
			    alternative->lock_member,
			    alternative->lock_offset, &candidate) ||
			    !formal_argument(function->ep,
			    candidate.root, &argument))
				continue;
			(void) add_protection_alternative(&alternatives,
			    argument, candidate.member, candidate.offset);
		}
		if (add_protection_condition(function, caller_argument,
		    data_root, data_object,
		    condition->has_lock, condition->required_modes,
		    condition->lock_root, condition->lock_object,
		    lock.member, lock.offset, object.member, object.offset,
		    condition->pos, alternatives))
			changed = true;
		free_protection_alternatives(alternatives);
	}
	return (changed);
}

static bool
propagate_function_protection_conditions(struct function_info *function)
{
	struct block_info *block;
	bool changed = false;

	for (block = function->blocks; block != NULL; block = block->next) {
		struct analysis_state *state;
		struct instruction *insn;

		if (!block->reachable)
			continue;
		state = copy_analysis_state(block->in);
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb == NULL)
				continue;
			if (propagate_call_protection_conditions(function, state,
			    insn))
				changed = true;
			transfer_instruction(function, state, insn);
		} END_FOR_EACH_PTR(insn);
		free_analysis_state(state);
	}
	return (changed);
}

static void
check_read_only_access(struct function_info *function,
    struct analysis_state *state, struct instruction *insn)
{
	struct locklint_access access;
	struct locklint_access lock;
	struct locklint_data_policy policy;
	struct symbol *data;
	const char *name;
	enum visibility_state visibility;

	if (insn->opcode != OP_STORE ||
	    !locklint_get_instruction_access(function->tu, insn, &access) ||
	    !locklint_data_policy(&access, &policy, &lock) ||
	    !policy.read_only)
		return;
	visibility = get_visibility(state->visibility, &access);
	if (competition_absent(&state->competition) ||
	    visibility == VISIBILITY_INVISIBLE)
		return;
	data = access.member != NULL ? access.member : access.root;
	name = data != NULL && data->ident != NULL ?
	    show_ident(data->ident) : "<unknown>";
	if (competition_present(&state->competition) &&
	    visibility == VISIBILITY_VISIBLE) {
		warning(insn->access->pos,
		    "locklint: read-only data '%s' modified while visible "
		    "to competing threads", name);
	} else {
		warning(insn->access->pos,
		    "locklint: read-only data '%s' modified while it may be "
		    "visible to competing threads", name);
	}
}

static void
check_access(struct function_info *function, struct analysis_state *state,
    struct instruction *insn)
{
	struct locklint_access access;
	struct locklint_access lock;
	struct symbol *data_member;
	const char *data_name;
	enum protection_status status;
	unsigned int argument;
	unsigned int required_modes;

	if (!protected_access(function, insn, &access, &lock, &data_member,
	    &required_modes))
		return;
	status = get_protection_status(function, state, &access, required_modes,
	    &lock);
	if (status == PROTECTION_DEFINITE)
		return;
	if (status == PROTECTION_ABSENT) {
		if (defer_conditions(function)) {
			struct symbol *data_root;
			struct object_identity *data_object;

			if (condition_data_identity(function, &access, &argument,
			    &data_root, &data_object))
				return;
		}
	}
	data_name = data_member != NULL && data_member->ident != NULL ?
	    show_ident(data_member->ident) : "<unknown>";
	if (status == PROTECTION_PATH_DEPENDENT) {
		warning(insn->access->pos,
		    "locklint: protection for member '%s' is not "
		    "established on every path", data_name);
	} else {
		warning(insn->access->pos,
		    "locklint: protected member '%s' accessed without "
		    "%s '%s'", data_name, required_ownership(required_modes),
		    lock_name(&lock));
	}
}

/*
 * Replay a local acquisition prefix when several callee roles denote one
 * held caller lock.  The callee CFG preserves the operation order that the
 * independent prefix tables necessarily lose.
 */
static bool
contextual_acquisition_prefix_state(struct function_info *function,
    struct instruction *insn, struct function_info *callee,
    const struct acquisition_summary *summary, const struct state_entry *held,
    lock_state_t *state)
{
	struct mapped_transfer_role *roles = NULL;
	struct mapped_transfer_role **tail = &roles;
	const struct acquisition_candidate *candidate;
	struct alias_replay_frame frame = { 0 };
	struct transfer_target target = { 0 };

	if (summary->source_function != callee || summary->checkpoint == NULL)
		return (false);
	for (candidate = callee->acquisition_roles; candidate != NULL;
	    candidate = candidate->next) {
		struct mapped_transfer_role *role;
		struct locklint_access mapped;
		struct lock_transfer *transfer;

		if (!map_acquisition_role(function, insn, &candidate->role,
		    &mapped) || !same_lock(&held->lock, &mapped))
			continue;
		transfer = find_transfer(callee, &candidate->role);
		if (transfer == NULL) {
			free_mapped_transfer_roles(roles);
			return (false);
		}
		role = calloc(1, sizeof (*role));
		if (role == NULL)
			die("out of memory replaying acquisition prefix");
		role->transfer = transfer;
		*tail = role;
		tail = &role->next;
	}
	frame.function = callee;
	frame.next = NULL;
	target.roles = roles;
	target.replay = &frame;
	*state = simulate_lock_prefix_target(callee, &target, held->state,
	    summary->checkpoint, NULL);
	free_mapped_transfer_roles(roles);
	return (true);
}

static lock_state_t
acquisition_prefix_state(struct function_info *function,
    struct instruction *insn, struct function_info *callee,
    const struct acquisition_summary *summary, const struct state_entry *held,
    bool *known, bool *transformed)
{
	const struct acquisition_candidate *candidate;
	const struct acquisition_prefix *prefix;
	lock_state_t state = 0;
	unsigned int aliases = 0;
	unsigned int matches = 0;
	bool found = false;

	for (candidate = callee->acquisition_roles; candidate != NULL;
	    candidate = candidate->next) {
		struct locklint_access mapped;

		if (map_acquisition_role(function, insn, &candidate->role,
		    &mapped) && same_lock(&held->lock, &mapped))
			aliases++;
	}
	if (aliases > 1) {
		if (contextual_acquisition_prefix_state(function, insn, callee,
		    summary, held, &state)) {
			*known = true;
			*transformed = state != held->state;
			return (state);
		}
	}

	for (prefix = summary->prefixes; prefix != NULL;
	    prefix = prefix->next) {
		struct locklint_access mapped;

		if (!map_acquisition_role(function, insn, &prefix->role,
		    &mapped) || !same_lock(&held->lock, &mapped))
			continue;
		if (prefix->unknown) {
			*known = false;
			*transformed = false;
			return (held->state);
		}
		state |= prefix->output[held->state];
		found = true;
		matches++;
	}
	if (matches > 1) {
		*known = false;
		*transformed = false;
		return (held->state);
	}
	*known = true;
	*transformed = found;
	return (found ? state : held->state);
}

static void
check_call_acquisitions(struct function_info *function,
    struct analysis_state *analysis, struct instruction *insn,
    struct function_info *callee, struct position pos)
{
	struct acquisition_summary *summary;

	for (summary = callee->acquisitions; summary != NULL;
	    summary = summary->next) {
		struct locklint_access acquired;
		struct state_entry *held;

		if (!map_acquisition_role(function, insn, &summary->acquired,
		    &acquired))
			continue;
		for (held = analysis->locks; held != NULL; held = held->next) {
			const struct position *held_pos;
			lock_state_t state;
			bool explained;
			bool known;
			bool transformed;

			if (locklint_same_access(&acquired, &held->lock))
				continue;
			state = acquisition_prefix_state(function, insn, callee,
			    summary, held, &known, &transformed);
			if (!known || (state & LOCK_ANY_HELD) == 0)
				continue;
			explained = locklint_order_check_declared(&acquired,
			    &held->lock, &pos, !state_definitely_held(state));
			held_pos = !transformed && held->has_acquire_pos ?
			    &held->acquire_pos : NULL;
			locklint_order_record_observed(&held->lock, &acquired,
			    held_pos, &summary->pos,
			    !state_definitely_held(state), explained);
			if (explained) {
				info(summary->pos, "locklint: lock acquisition "
				    "reached through callee '%s'",
				    show_ident(callee->ep->name->ident));
			}
		}
	}
}

static const char *
assertion_requirement_name(unsigned int modes)
{
	switch (modes) {
	case LOCK_NOT_HELD:
		return ("not-held");
	case LOCK_HELD:
		return ("mutex-held");
	case LOCK_READ_HELD:
		return ("read-held");
	case LOCK_WRITE_HELD:
		return ("write-held");
	case LOCK_READ_HELD | LOCK_WRITE_HELD:
		return ("read-or-write-held");
	case LOCK_NOT_HELD | LOCK_WRITE_HELD:
		return ("not-read-held");
	case LOCK_NOT_HELD | LOCK_READ_HELD:
		return ("not-write-held");
	default:
		return ("lock-state");
	}
}

static bool
assertion_input_mask_accepts(unsigned int accepted_inputs,
    lock_state_t state)
{
	return ((accepted_inputs & (1U << state)) != 0);
}

static bool
assertion_input_mask_accepts_part(unsigned int accepted_inputs,
    lock_state_t state)
{
	lock_state_t mode;

	for (mode = 1; mode < LOCK_STATE_COUNT; mode <<= 1) {
		if ((state & mode) != 0 &&
		    assertion_input_mask_accepts(accepted_inputs, mode))
			return (true);
	}
	return (false);
}

/*
 * Propagate one alias-role set through a wrapper while treating every role
 * and the asserted role as one lock.  This preserves wrapper-prefix
 * operations that become relevant only under that caller alias partition.
 */
static unsigned int
assertion_group_call_accepted_inputs(struct function_info *function,
    struct instruction *insn, const struct acquisition_role *role,
    const struct assertion_alternative_role *alternatives,
    unsigned int callee_accepted_inputs, bool *reachable)
{
	const struct assertion_alternative_role *alternative;
	struct lock_transfer *transfers;
	struct mapped_transfer_role *mapped;
	struct alias_replay_frame frame = { 0 };
	struct transfer_target target = { 0 };
	unsigned int accepted_inputs = 0;
	unsigned int count = 1;
	unsigned int index = 0;
	unsigned int input;

	for (alternative = alternatives; alternative != NULL;
	    alternative = alternative->next)
		count++;
	transfers = calloc(count, sizeof (*transfers));
	mapped = calloc(count, sizeof (*mapped));
	if (transfers == NULL || mapped == NULL)
		die("out of memory propagating assertion alias roles");
	transfers[index].role = *role;
	mapped[index].transfer = &transfers[index];
	index++;
	for (alternative = alternatives; alternative != NULL;
	    alternative = alternative->next) {
		transfers[index].role = alternative->role;
		mapped[index].transfer = &transfers[index];
		mapped[index - 1].next = &mapped[index];
		index++;
	}
	frame.function = function;
	target.roles = mapped;
	target.replay = &frame;
	*reachable = false;
	for (input = 1; input < LOCK_STATE_COUNT; input++) {
		lock_state_t state;

		state = simulate_lock_prefix_target(function, &target, input,
		    insn, reachable);
		if (assertion_input_mask_accepts(callee_accepted_inputs,
		    state))
			accepted_inputs |= 1U << input;
	}
	free(mapped);
	free(transfers);
	return (accepted_inputs);
}

struct assertion_role_mapping {
	struct acquisition_role source;
	struct acquisition_role mapped;
	struct assertion_role_mapping *next;
};

static bool
assertion_role_selected(const struct assertion_alternative_role *roles,
    const struct acquisition_role *role)
{
	for (; roles != NULL; roles = roles->next) {
		if (same_acquisition_role(&roles->role, role))
			return (true);
	}
	return (false);
}

static void
free_assertion_role_mappings(struct assertion_role_mapping *mappings)
{
	while (mappings != NULL) {
		struct assertion_role_mapping *next = mappings->next;

		free(mappings);
		mappings = next;
	}
}

/*
 * Map the callee's complete alternative-role universe into one caller.  The
 * external list contains each distinct caller role that could later alias the
 * mapped primary role; mappings to the primary are already internal aliases.
 */
static struct assertion_role_mapping *
map_assertion_alternative_roles(struct function_info *function,
    struct instruction *insn, const struct assertion_requirement *requirement,
    const struct acquisition_role *primary,
    struct assertion_alternative_role **external)
{
	struct assertion_role_mapping **tail;
	const struct assertion_alternative *alternative;
	struct assertion_role_mapping *mappings = NULL;

	tail = &mappings;
	for (alternative = requirement->alternatives; alternative != NULL;
	    alternative = alternative->next) {
		const struct assertion_alternative_role *source;

		for (source = alternative->roles; source != NULL;
		    source = source->next) {
			struct assertion_role_mapping *mapping;
			struct locklint_access lock;

			for (mapping = mappings; mapping != NULL;
			    mapping = mapping->next) {
				if (same_acquisition_role(&mapping->source,
				    &source->role))
					break;
			}
			if (mapping != NULL ||
			    !map_acquisition_role(function, insn,
			    &source->role, &lock))
				continue;
			mapping = calloc(1, sizeof (*mapping));
			if (mapping == NULL)
				die("out of memory mapping assertion alias roles");
			mapping->source = source->role;
			if (!acquisition_role(function, &lock,
			    &mapping->mapped)) {
				free(mapping);
				continue;
			}
			*tail = mapping;
			tail = &mapping->next;
			if (!same_acquisition_role(primary, &mapping->mapped))
				(void) add_assertion_alternative_role(external,
				    &mapping->mapped);
		}
	}
	return (mappings);
}

struct assertion_propagation_context {
	struct function_info *function;
	struct instruction *insn;
	const struct assertion_requirement *requirement;
	const struct acquisition_role *primary;
	const struct assertion_role_mapping *mappings;
	struct assertion_alternative **alternatives;
	unsigned int accepted_inputs;
	bool reachable;
};

/*
 * Compose every subset of mapped caller roles through one wrapper call.
 * Source roles that collapse to the same selected caller role are looked up
 * together in the callee's exact role-set table.
 */
static void
propagate_assertion_subsets(struct assertion_propagation_context *context,
    const struct assertion_alternative_role *candidate,
    struct assertion_alternative_role *selected)
{
	if (candidate == NULL) {
		const struct assertion_role_mapping *mapping;
		struct assertion_alternative_role *callee_roles = NULL;
		const struct assertion_alternative *alternative;
		unsigned int callee_inputs;
		unsigned int accepted_inputs;
		bool reachable;

		for (mapping = context->mappings; mapping != NULL;
		    mapping = mapping->next) {
			if (same_acquisition_role(context->primary,
			    &mapping->mapped) ||
			    assertion_role_selected(selected, &mapping->mapped))
				(void) add_assertion_alternative_role(
				    &callee_roles, &mapping->source);
		}
		if (callee_roles == NULL) {
			callee_inputs =
			    context->requirement->accepted_inputs;
		} else {
			alternative = find_assertion_alternative(
			    context->requirement->alternatives, callee_roles);
			callee_inputs = alternative != NULL ?
			    alternative->accepted_inputs :
			    context->requirement->accepted_inputs;
		}
		accepted_inputs = assertion_group_call_accepted_inputs(
		    context->function, context->insn, context->primary,
		    selected, callee_inputs, &reachable);
		free_assertion_alternative_roles(callee_roles);
		if (!reachable)
			return;
		context->reachable = true;
		if (selected == NULL) {
			context->accepted_inputs = accepted_inputs;
		} else {
			(void) add_assertion_alternative(
			    context->alternatives, selected, accepted_inputs);
		}
		return;
	}
	propagate_assertion_subsets(context, candidate->next, selected);
	{
		struct assertion_alternative_role included = {
			.role = candidate->role,
			.next = selected
		};

		propagate_assertion_subsets(context, candidate->next,
		    &included);
	}
}

static bool
propagate_assertion_requirements(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct function_info *callee;
			struct assertion_requirement *requirement;

			if (insn->bb == NULL)
				continue;
			callee = callgraph_callee(function, insn);
			if (callee == NULL)
				continue;
			for (requirement = callee->assertion_requirements;
			    requirement != NULL; requirement = requirement->next) {
				struct acquisition_role role;
				struct assertion_alternative *alternatives = NULL;
				struct assertion_alternative_role *external = NULL;
				struct assertion_role_mapping *mappings;
				struct assertion_propagation_context context;
				struct locklint_access lock;

				if (!map_acquisition_role(function, insn,
				    &requirement->role, &lock) ||
				    !acquisition_role(function, &lock, &role))
					continue;
				mappings = map_assertion_alternative_roles(function,
				    insn, requirement, &role, &external);
				memset(&context, 0, sizeof (context));
				context.function = function;
				context.insn = insn;
				context.requirement = requirement;
				context.primary = &role;
				context.mappings = mappings;
				context.alternatives = &alternatives;
				propagate_assertion_subsets(&context, external,
				    NULL);
				if (context.reachable &&
				    add_assertion_requirement(function, &role,
				    requirement->modes,
				    context.accepted_inputs,
				    requirement->origin_tu, requirement->pos,
				    requirement->source_function,
				    requirement->checkpoint, alternatives))
					changed = true;
				free_assertion_alternatives(alternatives);
				free_assertion_alternative_roles(external);
				free_assertion_role_mappings(mappings);
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
}

/*
 * Evaluate a local callee assertion contextually when another transfer role
 * maps to the same caller lock.  The asserted role is represented by a
 * synthetic target entry; matching transfer roles add the operations whose
 * factored prefixes would otherwise be lost.
 */
static bool
assertion_alias_call_state(struct function_info *function,
    struct instruction *insn, struct function_info *callee,
    const struct assertion_requirement *requirement, lock_state_t input,
    lock_state_t *output)
{
	struct lock_transfer assertion_transfer = { 0 };
	struct mapped_transfer_role assertion_role = { 0 };
	struct mapped_transfer_role **tail = &assertion_role.next;
	struct mapped_transfer_role *roles;
	struct lock_transfer *transfer;
	struct alias_replay_frame frame = { 0 };
	struct transfer_target target = { 0 };
	struct locklint_access requirement_lock;
	bool reachable;

	if (requirement->source_function != callee ||
	    requirement->checkpoint == NULL ||
	    !map_acquisition_role(function, insn, &requirement->role,
	    &requirement_lock))
		return (false);
	for (transfer = callee->transfers; transfer != NULL;
	    transfer = transfer->next) {
		struct mapped_transfer_role *role;
		struct locklint_access lock;

		if (same_acquisition_role(&transfer->role,
		    &requirement->role) ||
		    !map_acquisition_role(function, insn, &transfer->role,
		    &lock) || !same_lock(&lock, &requirement_lock))
			continue;
		role = calloc(1, sizeof (*role));
		if (role == NULL)
			die("out of memory replaying aliased assertion");
		role->transfer = transfer;
		*tail = role;
		tail = &role->next;
	}
	if (assertion_role.next == NULL)
		return (false);

	assertion_transfer.role = requirement->role;
	assertion_role.transfer = &assertion_transfer;
	frame.function = callee;
	frame.next = NULL;
	target.roles = &assertion_role;
	target.replay = &frame;
	*output = simulate_lock_prefix_target(callee, &target, input,
	    requirement->checkpoint, &reachable);
	roles = assertion_role.next;
	assertion_role.next = NULL;
	free_mapped_transfer_roles(roles);
	return (reachable);
}

static bool
assertion_alias_accepted_inputs(struct function_info *function,
    struct instruction *insn, const struct assertion_requirement *requirement,
    const struct locklint_access *requirement_lock,
    unsigned int *accepted_inputs)
{
	const struct assertion_alternative *alternative;
	struct assertion_alternative_role *matched = NULL;

	for (alternative = requirement->alternatives; alternative != NULL;
	    alternative = alternative->next) {
		const struct assertion_alternative_role *role;

		for (role = alternative->roles; role != NULL;
		    role = role->next) {
			struct locklint_access lock;

			if (!map_acquisition_role(function, insn, &role->role,
			    &lock) || !same_lock(&lock, requirement_lock))
				continue;
			(void) add_assertion_alternative_role(&matched,
			    &role->role);
		}
	}
	if (matched == NULL)
		return (false);
	alternative = find_assertion_alternative(
	    requirement->alternatives, matched);
	free_assertion_alternative_roles(matched);
	if (alternative == NULL)
		return (false);
	*accepted_inputs = alternative->accepted_inputs;
	return (true);
}

static void
check_call_assertion_requirements(struct function_info *function,
    struct analysis_state *analysis, struct instruction *insn,
    struct function_info *callee, struct position pos)
{
	struct assertion_requirement *requirement;

	for (requirement = callee->assertion_requirements;
	    requirement != NULL; requirement = requirement->next) {
		struct locklint_access lock;
		lock_state_t state;
		const char *name;
		unsigned int accepted_inputs;
		bool accepted;
		bool accepted_part;

		if (!map_acquisition_role(function, insn, &requirement->role,
		    &lock))
			continue;
		state = get_state(analysis->locks, &lock);
		if (assertion_alias_call_state(function, insn, callee,
		    requirement, state, &state)) {
			accepted = (state & ~requirement->modes) == 0;
			accepted_part = (state & requirement->modes) != 0;
		} else {
			if (!assertion_alias_accepted_inputs(function, insn,
			    requirement, &lock, &accepted_inputs))
				accepted_inputs = requirement->accepted_inputs;
			accepted = assertion_input_mask_accepts(
			    accepted_inputs, state);
			accepted_part = assertion_input_mask_accepts_part(
			    accepted_inputs, state);
		}
		if (accepted)
			continue;
		{
			struct acquisition_role role;

			if (defer_conditions(function) &&
			    acquisition_role(function, &lock, &role))
				continue;
		}
		name = assertion_requirement_name(requirement->modes);
		if (accepted_part) {
			warning(pos, "locklint: asserted %s requirement for "
			    "lock '%s' is not established on every path "
			    "calling '%s'", name, lock_name(&lock),
			    show_ident(callee->ep->name->ident));
		} else {
			warning(pos, "locklint: call to '%s' does not satisfy "
			    "asserted %s requirement for lock '%s'",
			    show_ident(callee->ep->name->ident), name,
			    lock_name(&lock));
		}
		info(requirement->pos, "locklint: asserted requirement is here");
	}
}

static void
check_call(struct function_info *function,
    struct analysis_state *analysis, struct instruction *insn)
{
	struct function_info *callee = callgraph_callee(function, insn);
	struct protection_condition *condition;
	struct position pos;

	if (callee == NULL) {
		if (callgraph_ambiguous_callee(function, insn)) {
			pos = insn->call_expr != NULL ?
			    insn->call_expr->pos : insn->pos;
			warning(pos, "locklint: direct call has multiple "
			    "external definitions");
		}
		return;
	}
	pos = insn->call_expr != NULL ? insn->call_expr->pos : insn->pos;
	check_call_assertion_requirements(function, analysis, insn, callee, pos);
	if (callee != function) {
		for (condition = callee->conditions; condition != NULL;
		    condition = condition->next) {
			struct locklint_access object;
			struct locklint_access lock;
			enum protection_status status;
			const char *data_name;
			unsigned int caller_argument;

			if (!map_call_protection_condition(function, insn,
			    condition, &object, &lock))
				continue;
			if (condition_alternative_satisfied(function, insn,
			    condition, &lock))
				continue;
			status = get_protection_status(function, analysis, &object,
			    condition->required_modes, &lock);
			if (status == PROTECTION_DEFINITE)
				continue;
			if (status == PROTECTION_ABSENT &&
			    defer_conditions(function)) {
				struct symbol *data_root;
				struct object_identity *data_object;

				if (condition_data_identity(function, &object,
				    &caller_argument, &data_root, &data_object))
					continue;
			}
			data_name = access_name(&object);
			if (status == PROTECTION_ABSENT) {
				if (condition->has_lock) {
					warning(pos, "locklint: call to '%s' "
					    "accesses protected member '%s' "
					    "without %s '%s'",
					    show_ident(callee->ep->name->ident),
					    data_name, required_ownership(
					    condition->required_modes),
					    lock_name(&lock));
				} else {
					warning(pos, "locklint: call to '%s' "
					    "requires protection for '%s'",
					    show_ident(callee->ep->name->ident),
					    data_name);
				}
			} else {
				warning(pos, "locklint: protection for member "
				    "'%s' is not established on every path "
				    "calling '%s'", data_name,
				    show_ident(callee->ep->name->ident));
			}
		}
	}
	{
		struct mapped_lock_transfer *mapped;
		struct mapped_lock_transfer *group;

		check_call_acquisitions(function, analysis, insn, callee, pos);
		if (callee->transfers != NULL &&
		    callee->transfers->next == NULL) {
			struct lock_transfer *transfer = callee->transfers;
			struct locklint_access lock;
			lock_state_t state;
			unsigned int invalid;
			bool defer;

			if (!map_acquisition_role(function, insn,
			    &transfer->role, &lock))
				goto lock_transfers_done;
			state = get_state(analysis->locks, &lock);
			invalid = transfer->invalid[state];
			defer = defer_lock_diagnostics(function, &lock);
			if (!defer && (invalid & INVALID_ACQUIRE) != 0) {
				warning(pos, "locklint: call to '%s' may acquire "
				    "already-held lock '%s'",
				    show_ident(callee->ep->name->ident),
				    lock_name(&lock));
			}
			if (!defer && (invalid & INVALID_RELEASE) != 0) {
				warning(pos, "locklint: call to '%s' may release "
				    "lock '%s' that is not held",
				    show_ident(callee->ep->name->ident),
				    lock_name(&lock));
			}
			set_state(&analysis->locks, &lock,
			    transfer->output[state]);
			goto lock_transfers_done;
		}
		mapped = map_call_lock_transfers(function, insn, callee);
		for (group = mapped; group != NULL; group = group->next) {
			unsigned int invalid;
			lock_state_t state;
			bool defer;

			state = get_state(analysis->locks, &group->lock);
			if (group->role_count == 1) {
				struct lock_transfer *transfer =
				    group->roles->transfer;

				invalid = transfer->invalid[state];
				state = transfer->output[state];
			} else {
				state = simulate_aliased_transfer(callee,
				    group->roles, state, &invalid);
			}
			defer = defer_lock_diagnostics(function, &group->lock);
			if (!defer &&
			    (invalid & INVALID_ACQUIRE) != 0) {
				warning(pos, "locklint: call to '%s' may acquire "
				    "already-held lock '%s'",
				    show_ident(callee->ep->name->ident),
				    lock_name(&group->lock));
			}
			if (!defer &&
			    (invalid & INVALID_RELEASE) != 0) {
				warning(pos, "locklint: call to '%s' may release "
				    "lock '%s' that is not held",
				    show_ident(callee->ep->name->ident),
				    lock_name(&group->lock));
			}
			set_state(&analysis->locks, &group->lock, state);
		}
		free_mapped_lock_transfers(mapped);
lock_transfers_done:
		;
	}
	transfer_call_visibility_effects(function, &analysis->visibility, insn);
	if (callee->competition_transfer != NULL) {
		struct competition_transfer *transfer =
		    callee->competition_transfer;
		bool underflows;

		if (analysis->competition.ambient) {
			underflows =
			    transfer->ambient_minimum_prefix_unbounded ||
			    transfer->ambient_minimum_prefix < 0;
		} else {
			underflows = transfer->minimum_prefix_unbounded;
			if (!underflows && transfer->minimum_prefix < 0) {
				underflows =
				    analysis->competition.minimum_unbounded ||
				    analysis->competition.minimum +
				    transfer->minimum_prefix < 0;
			}
		}
		if (underflows && !defer_conditions(function)) {
			warning(pos, "locklint: call to '%s' may decrement "
			    "competition depth below zero",
			    show_ident(callee->ep->name->ident));
		}
		apply_competition_transfer(&analysis->competition, transfer);
	}
}

static void
check_lock_action(struct function_info *function,
    struct analysis_state *analysis, struct instruction *insn)
{
	struct locklint_access lock;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;
	lock_state_t state;
	struct position pos;
	bool defer;

	action = locklint_get_lock_action(function->tu, insn, &lock, &mode);
	if (action == LOCKLINT_LOCK_NONE || lock.root == NULL)
		return;
	state = get_state(analysis->locks, &lock);
	defer = defer_lock_diagnostics(function, &lock);
	pos = insn->call_expr != NULL ? insn->call_expr->pos : insn->pos;
	if (action == LOCKLINT_LOCK_ACQUIRE) {
		struct state_entry *held;

		for (held = analysis->locks; held != NULL; held = held->next) {
			bool explained;

			if ((held->state & LOCK_ANY_HELD) == 0 ||
			    locklint_same_access(&lock, &held->lock))
				continue;
			explained = locklint_order_check_declared(&lock,
			    &held->lock, &pos,
			    !state_definitely_held(held->state));
			locklint_order_record_observed(&held->lock, &lock,
			    held->has_acquire_pos ? &held->acquire_pos : NULL,
			    &pos, !state_definitely_held(held->state),
			    explained);
		}
		if (state_definitely_held(state) && !defer) {
			warning(pos, "locklint: lock '%s' is already held",
			    lock_name(&lock));
		} else if (state_maybe_held(state) && !defer) {
			warning(pos, "locklint: lock '%s' may already be held",
			    lock_name(&lock));
		}
		set_state(&analysis->locks, &lock, mode);
		set_acquire_position(analysis->locks, &lock, pos);
	} else {
		if (state == LOCK_NOT_HELD && !defer) {
			warning(pos, "locklint: lock '%s' is not held",
			    lock_name(&lock));
		} else if ((state & LOCK_NOT_HELD) != 0 && !defer) {
			warning(pos, "locklint: lock '%s' may not be held",
			    lock_name(&lock));
		}
		set_state(&analysis->locks, &lock, LOCK_NOT_HELD);
	}
}

static bool
function_declares_lock_effect(struct function_info *function,
    const struct locklint_access *target)
{
	struct basic_block *bb;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct acquisition_role role;
			struct locklint_access lock;
			enum locklint_declared_lock_effect effect;

			if (insn->bb != NULL &&
			    locklint_get_declared_lock_effect(function->tu,
			    insn, &effect, &lock) &&
			    acquisition_role(function, &lock, &role) &&
			    same_lock(target, &role.access))
				return (true);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (false);
}

static void
check_competition_transition(struct function_info *function,
    const struct analysis_state *state, const struct instruction *insn)
{
	struct position pos;

	if (locklint_get_execution_annotation(insn) !=
	    LOCKLINT_EXECUTION_NO_COMPETITION ||
	    state->competition.ambient ||
	    (!state->competition.minimum_unbounded &&
	    state->competition.minimum > 0) ||
	    defer_conditions(function))
		return;
	pos = insn->pos;
	if (!state->competition.maximum_unbounded &&
	    state->competition.maximum <= 0) {
		warning(pos, "locklint: competition depth decremented below zero");
	} else {
		warning(pos, "locklint: competition depth may be decremented "
		    "below zero");
	}
}

static void
check_return_state(struct function_info *function, struct block_info *block,
    struct analysis_state *state)
{
	struct instruction *insn;
	struct instruction *ret = NULL;
	struct state_entry *entry;
	struct position pos;

	FOR_EACH_PTR(block->bb->insns, insn) {
		if (insn->bb != NULL && insn->opcode == OP_RET)
			ret = insn;
	} END_FOR_EACH_PTR(insn);
	if (ret == NULL)
		return;
	pos = ret->pos;
	for (entry = state->locks; entry != NULL; entry = entry->next) {
		if (!entry->side_effect)
			continue;
		if (function_declares_lock_effect(function, &entry->lock))
			continue;
		if (state_definitely_held(entry->state)) {
			warning(pos, "locklint: lock '%s' held on return from '%s'",
			    lock_name(&entry->lock),
			    show_ident(function->ep->name->ident));
		} else {
			warning(pos, "locklint: lock '%s' held on only some paths "
			    "returning from '%s'", lock_name(&entry->lock),
			    show_ident(function->ep->name->ident));
		}
	}
}

static void
emit_diagnostics(struct function_info *function)
{
	struct block_info *block;

	for (block = function->blocks; block != NULL; block = block->next) {
		struct analysis_state *state;
		struct instruction *insn;

		if (!block->reachable)
			continue;
		state = copy_analysis_state(block->in);
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb == NULL)
				continue;
			check_read_only_access(function, state, insn);
			check_access(function, state, insn);
			check_call(function, state, insn);
			check_lock_action(function, state, insn);
			transfer_assertion(function, &state->locks, insn);
			check_competition_transition(function, state, insn);
			transfer_competition_state(state, insn);
			transfer_visibility_state(function, state, insn, true);
			diagnose_assumption(function, insn);
		} END_FOR_EACH_PTR(insn);
		check_return_state(function, block, state);
		free_analysis_state(state);
	}
}

static void
free_blocks(struct block_info *blocks)
{
	while (blocks != NULL) {
		struct block_info *next = blocks->next;

		free_analysis_state(blocks->in);
		free_analysis_state(blocks->out);
		free(blocks);
		blocks = next;
	}
}

static struct callgraph_iter *
function_iter_open(void)
{
	struct callgraph_iter *iter;
	int error;

	error = callgraph_iter_open(&iter);
	if (error != 0)
		die("cannot iterate ready callgraph: %s", strerror(error));
	return (iter);
}

/*
 * Stabilize transfer summaries before block state, then stabilize caller
 * protection conditions before replaying the final states to emit diagnostics.
 */
static void
run_lock_checks(void)
{
	struct callgraph_iter *iter;
	struct function_info *function;
	unsigned int function_count = 0;
	unsigned int competition_round;
	bool changed;

	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL) {
		struct competition_transfer *competition_transfer;

		competition_transfer = calloc(1,
		    sizeof (*competition_transfer));
		if (competition_transfer == NULL)
			die("out of memory recording competition transfer");
		competition_transfer->ambient_output.maximum = 1;
		competition_transfer->ambient_output.ambient = true;
		function->competition_transfer = competition_transfer;
		function_count++;
		(void) collect_local_transfers(function);
		(void) collect_local_visibility_transfers(function);
	}
	callgraph_iter_close(iter);
	do {
		changed = false;
		iter = function_iter_open();
		while ((function = callgraph_iter_next(iter)) != NULL) {
			if (propagate_transfer_candidates(function))
				changed = true;
			if (propagate_visibility_transfer_candidates(function))
				changed = true;
		}
		callgraph_iter_close(iter);
		iter = function_iter_open();
		while ((function = callgraph_iter_next(iter)) != NULL) {
			if (solve_function_transfers(function))
				changed = true;
			if (solve_function_visibility_transfers(function))
				changed = true;
		}
		callgraph_iter_close(iter);
	} while (changed);
	competition_round = 0;
	do {
		changed = false;
		competition_round++;
		iter = function_iter_open();
		while ((function = callgraph_iter_next(iter)) != NULL) {
			if (solve_function_competition_transfer(function,
			    competition_round > function_count))
				changed = true;
		}
		callgraph_iter_close(iter);
	} while (changed);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL)
		collect_assertion_requirements(function);
	callgraph_iter_close(iter);
	do {
		changed = false;
		iter = function_iter_open();
		while ((function = callgraph_iter_next(iter)) != NULL) {
			if (propagate_assertion_requirements(function))
				changed = true;
		}
		callgraph_iter_close(iter);
	} while (changed);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL) {
		validate_declared_effects(function);
		validate_declared_competition_effects(function);
	}
	callgraph_iter_close(iter);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL)
		collect_function_assumptions(function);
	callgraph_iter_close(iter);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL)
		function->blocks = analyze_blocks(function);
	callgraph_iter_close(iter);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL)
		collect_local_acquisition_roles(function);
	callgraph_iter_close(iter);
	do {
		changed = false;
		iter = function_iter_open();
		while ((function = callgraph_iter_next(iter)) != NULL) {
			if (propagate_acquisition_roles(function))
				changed = true;
		}
		callgraph_iter_close(iter);
	} while (changed);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL)
		collect_local_acquisition_summaries(function);
	callgraph_iter_close(iter);
	do {
		changed = false;
		iter = function_iter_open();
		while ((function = callgraph_iter_next(iter)) != NULL) {
			if (propagate_acquisition_summaries(function))
				changed = true;
		}
		callgraph_iter_close(iter);
	} while (changed);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL)
		collect_local_protection_conditions(function);
	callgraph_iter_close(iter);
	do {
		changed = false;
		iter = function_iter_open();
		while ((function = callgraph_iter_next(iter)) != NULL) {
			if (propagate_function_protection_conditions(function))
				changed = true;
		}
		callgraph_iter_close(iter);
	} while (changed);
	iter = function_iter_open();
	while ((function = callgraph_iter_next(iter)) != NULL)
		emit_diagnostics(function);
	callgraph_iter_close(iter);
	locklint_order_report_observed_cycles();
}

static void
free_checker_attachments(void)
{
	struct callgraph_iter *iter = function_iter_open();
	struct function_info *function;

	while ((function = callgraph_iter_next(iter)) != NULL) {
		struct acquisition_candidate *acquisition_role =
		    function->acquisition_roles;
		struct acquisition_summary *acquisition =
		    function->acquisitions;
		struct assertion_requirement *requirement =
		    function->assertion_requirements;
		struct competition_transfer *competition_transfer =
		    function->competition_transfer;
		struct protection_condition *condition = function->conditions;
		struct assumed_region *assumption = function->assumptions;
		struct lock_transfer *transfer = function->transfers;
		struct visibility_transfer *visibility_transfer =
		    function->visibility_transfers;

		while (condition != NULL) {
			struct protection_condition *condition_next =
			    condition->next;

			free_protection_alternatives(condition->alternatives);
			free(condition);
			condition = condition_next;
		}
		while (assumption != NULL) {
			struct assumed_region *assumption_next = assumption->next;

			free(assumption);
			assumption = assumption_next;
		}
		while (requirement != NULL) {
			struct assertion_requirement *requirement_next =
			    requirement->next;

			free_assertion_alternatives(requirement->alternatives);
			free(requirement);
			requirement = requirement_next;
		}
		free_acquisition_candidates(acquisition_role);
		while (acquisition != NULL) {
			struct acquisition_summary *acquisition_next =
			    acquisition->next;
			struct acquisition_prefix *prefix =
			    acquisition->prefixes;

			while (prefix != NULL) {
				struct acquisition_prefix *prefix_next =
				    prefix->next;

				free(prefix);
				prefix = prefix_next;
			}
			free(acquisition);
			acquisition = acquisition_next;
		}
		while (transfer != NULL) {
			struct lock_transfer *transfer_next = transfer->next;

			free(transfer);
			transfer = transfer_next;
		}
		while (visibility_transfer != NULL) {
			struct visibility_transfer *transfer_next =
			    visibility_transfer->next;

			free(visibility_transfer);
			visibility_transfer = transfer_next;
		}
		free(competition_transfer);
		free_blocks(function->blocks);
		function->blocks = NULL;
		function->conditions = NULL;
		function->assumptions = NULL;
		function->assertion_requirements = NULL;
		function->competition_transfer = NULL;
		function->acquisition_roles = NULL;
		function->acquisitions = NULL;
		function->transfers = NULL;
		function->visibility_transfers = NULL;
	}
	callgraph_iter_close(iter);
}

/*
 * Complete module-wide resolution, run the requested audit and checks, then
 * release checker attachments before destroying the callgraph.
 */
void
locklint_check_all(bool check_locks, bool show_callgraph)
{
	callgraph_resolve();
	if (show_callgraph)
		callgraph_dump(stdout);
	if (check_locks) {
		locklint_order_build();
		locklint_order_report_declared_cycles();
		run_lock_checks();
		locklint_order_cleanup();
	}
	free_checker_attachments();
	callgraph_cleanup();
}
