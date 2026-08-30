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
#include "avl.h"
#include "dissect.h"
#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "annotations.h"
#include "assertions.h"
#include "check.h"
#include "events.h"
#include "identity.h"
#include "symbol.h"

typedef unsigned int lock_state_t;

#define	LOCK_NOT_HELD	LOCKLINT_MODE_UNHELD
#define	LOCK_HELD	LOCKLINT_MODE_MUTEX
#define	LOCK_READ_HELD	LOCKLINT_MODE_READER
#define	LOCK_WRITE_HELD	LOCKLINT_MODE_WRITER
#define	LOCK_STATE_COUNT	(1 << 4)

#define	LOCK_ANY_HELD	(LOCK_HELD | LOCK_READ_HELD | LOCK_WRITE_HELD)

enum competition_state {
	COMPETITION_NONE,
	COMPETITION_POSSIBLE,
	COMPETITION_PRESENT
};

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

enum function_root_reason {
	FUNCTION_ROOT_EXTERNAL = 1 << 0,
	FUNCTION_ROOT_NO_DIRECT_CALLER = 1 << 1,
	FUNCTION_ROOT_POINTER_ESCAPE = 1 << 2
};

#define	INVALID_ACQUIRE	0x1
#define	INVALID_RELEASE	0x2

struct state_entry {
	struct locklint_access lock;
	lock_state_t state;
	bool side_effect;
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

struct analysis_state {
	struct state_entry *locks;
	enum competition_state competition;
	bool competition_path_dependent;
	struct visibility_entry *visibility;
};

struct block_info {
	struct basic_block *bb;
	bool reachable;
	struct analysis_state *in;
	struct analysis_state *out;
	struct block_info *next;
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
	struct protection_condition *next;
};

struct lock_transfer {
	unsigned int argument;
	struct symbol *lock_member;
	unsigned long lock_offset;
	lock_state_t output[LOCK_STATE_COUNT];
	unsigned int invalid[LOCK_STATE_COUNT];
	struct lock_transfer *next;
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

struct function_info {
	/* Functions and their retained Sparse objects belong to one parse. */
	struct translation_unit *tu;
	struct entrypoint *ep;
	struct block_info *blocks;
	struct protection_condition *conditions;
	struct assumed_region *assumptions;
	struct lock_transfer *transfers;
	struct visibility_transfer *visibility_transfers;
	unsigned int root_reasons;
	bool has_nonself_direct_caller;
	bool reachable_from_root;
	bool has_exact_escape;
	bool internal_linkage;
	bool identity_lower_bound;
	unsigned int identity_sequence;
	avl_node_t by_entrypoint;
	avl_node_t by_identity;
	struct function_info *next;
};

struct function_escape {
	struct translation_unit *tu;
	struct symbol *symbol;
	struct function_info *target;
	struct position pos;
	unsigned int sequence;
	avl_node_t by_source;
	avl_node_t by_target;
	struct function_escape *next;
};

enum function_pointer_activity_kind {
	FUNCTION_POINTER_LOAD,
	FUNCTION_POINTER_STORE
};

struct function_pointer_activity {
	struct translation_unit *tu;
	struct symbol *symbol;
	struct symbol *member;
	struct position pos;
	enum function_pointer_activity_kind kind;
	unsigned int sequence;
	avl_node_t by_source;
	struct function_pointer_activity *next;
};

struct call_audit {
	struct instruction *insn;
	struct position pos;
	unsigned int sequence;
};

static struct function_info *functions;
static struct function_info **functions_tail = &functions;
static avl_tree_t functions_by_entrypoint;
static avl_tree_t functions_by_identity;
static bool function_indexes_initialized;
static unsigned int next_function_identity_sequence;
static struct function_escape *function_escapes;
static struct function_escape **function_escapes_tail = &function_escapes;
static avl_tree_t function_escapes_by_source;
static avl_tree_t function_escapes_by_target;
static bool function_escape_indexes_initialized;
static unsigned int next_function_escape_sequence;
static struct translation_unit *function_escape_tu;
static struct function_pointer_activity *function_pointer_activities;
static struct function_pointer_activity **function_pointer_activities_tail =
    &function_pointer_activities;
static avl_tree_t function_pointer_activity_by_source;
static bool function_pointer_activity_index_initialized;
static bool record_function_pointer_activity;
static unsigned int next_function_pointer_activity_sequence;

static void transfer_call_effects(struct function_info *,
    struct state_entry **, struct instruction *);
static void transfer_call_visibility_effects(struct function_info *,
    struct visibility_entry **, struct instruction *);

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
		copy->competition_path_dependent =
		    state->competition_path_dependent;
		copy->visibility = copy_visibility(state->visibility);
	} else {
		copy->competition = COMPETITION_POSSIBLE;
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
same_analysis_state(const struct analysis_state *left,
    const struct analysis_state *right)
{
	return (same_states(left != NULL ? left->locks : NULL,
	    right != NULL ? right->locks : NULL) &&
	    (left == NULL ? COMPETITION_POSSIBLE : left->competition) ==
	    (right == NULL ? COMPETITION_POSSIBLE : right->competition) &&
	    (left != NULL && left->competition_path_dependent) ==
	    (right != NULL && right->competition_path_dependent) &&
	    same_visibility(left != NULL ? left->visibility : NULL,
	    right != NULL ? right->visibility : NULL));
}

static void
merge_analysis_state(struct analysis_state *merged,
    const struct analysis_state *incoming)
{
	merge_states(&merged->locks, incoming->locks);
	if (merged->competition != incoming->competition) {
		merged->competition = COMPETITION_POSSIBLE;
		merged->competition_path_dependent = true;
	} else {
		merged->competition_path_dependent |=
		    incoming->competition_path_dependent;
	}
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
	if (action == LOCKLINT_LOCK_ACQUIRE && lock.root != NULL)
		set_state(states, &lock, mode);
	else if (action == LOCKLINT_LOCK_RELEASE && lock.root != NULL)
		set_state(states, &lock, LOCK_NOT_HELD);
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
transfer_competition_state(struct analysis_state *state,
    const struct instruction *insn)
{
	switch (locklint_get_execution_annotation(insn)) {
	case LOCKLINT_EXECUTION_NO_COMPETITION:
		state->competition = COMPETITION_NONE;
		state->competition_path_dependent = false;
		break;
	case LOCKLINT_EXECUTION_COMPETITION:
		state->competition = COMPETITION_PRESENT;
		state->competition_path_dependent = false;
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
    bool *reachable)
{
	struct analysis_state *merged = NULL;
	struct basic_block *parent;
	bool first = true;

	*reachable = false;
	FOR_EACH_PTR(bb->parents, parent) {
		struct block_info *block = find_block(blocks, parent);

		if (block == NULL || !block->reachable)
			continue;
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
			bool reachable;

			if (block->bb == ep->entry->bb) {
				in = copy_analysis_state(NULL);
				reachable = true;
			} else {
				in = merge_parents(blocks, block->bb, &reachable);
			}
			if (!reachable) {
				free_analysis_state(in);
				continue;
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
		}
	} while (changed);

	return (blocks);
}

static int
compare_function_entrypoint(const void *left_arg, const void *right_arg)
{
	const struct function_info *left = left_arg;
	const struct function_info *right = right_arg;

	return (AVL_PCMP(left->ep, right->ep));
}

static int
compare_ident(struct ident *left, struct ident *right)
{
	size_t length;
	int result;

	if (left == right)
		return (0);
	if (left == NULL)
		return (-1);
	if (right == NULL)
		return (1);
	length = left->len < right->len ? left->len : right->len;
	result = memcmp(left->name, right->name, length);
	if (result != 0)
		return (AVL_ISIGN(result));
	return (AVL_CMP(left->len, right->len));
}

static int
compare_position(struct position left, struct position right)
{
	int result;

	result = AVL_CMP(left.stream, right.stream);
	if (result != 0)
		return (result);
	result = AVL_CMP(left.line, right.line);
	if (result != 0)
		return (result);
	return (AVL_CMP(left.pos, right.pos));
}

static int
compare_function_identity(const void *left_arg, const void *right_arg)
{
	const struct function_info *left = left_arg;
	const struct function_info *right = right_arg;
	const struct symbol *left_definition = left->ep->name;
	const struct symbol *right_definition = right->ep->name;
	int result;

	result = AVL_CMP(left->internal_linkage, right->internal_linkage);
	if (result != 0)
		return (result);
	result = compare_ident(left_definition->ident, right_definition->ident);
	if (result != 0)
		return (result);
	if (left->internal_linkage) {
		result = AVL_PCMP(left->tu, right->tu);
		if (result != 0)
			return (result);
	}
	/* A transient lower-bound key precedes every matching definition. */
	if (left->identity_lower_bound != right->identity_lower_bound)
		return (left->identity_lower_bound ? -1 : 1);
	if (left->identity_lower_bound)
		return (0);
	result = compare_position(left_definition->pos, right_definition->pos);
	if (result != 0)
		return (result);
	return (AVL_CMP(left->identity_sequence,
	    right->identity_sequence));
}

static int
compare_function_escape_source(const void *left_arg, const void *right_arg)
{
	const struct function_escape *left = left_arg;
	const struct function_escape *right = right_arg;
	int result;

	result = AVL_CMP(locklint_translation_unit_id(left->tu),
	    locklint_translation_unit_id(right->tu));
	if (result != 0)
		return (result);
	result = compare_position(left->pos, right->pos);
	if (result != 0)
		return (result);
	return (AVL_CMP(left->sequence, right->sequence));
}

static int
compare_function_escape_target(const void *left_arg, const void *right_arg)
{
	const struct function_escape *left = left_arg;
	const struct function_escape *right = right_arg;
	int result;

	result = AVL_PCMP(left->target, right->target);
	if (result != 0)
		return (result);
	result = compare_position(left->pos, right->pos);
	if (result != 0)
		return (result);
	return (AVL_CMP(left->sequence, right->sequence));
}

static int
compare_function_pointer_activity(const void *left_arg,
    const void *right_arg)
{
	const struct function_pointer_activity *left = left_arg;
	const struct function_pointer_activity *right = right_arg;
	int result;

	result = AVL_CMP(locklint_translation_unit_id(left->tu),
	    locklint_translation_unit_id(right->tu));
	if (result != 0)
		return (result);
	result = compare_position(left->pos, right->pos);
	if (result != 0)
		return (result);
	result = AVL_CMP(left->kind, right->kind);
	if (result != 0)
		return (result);
	return (AVL_CMP(left->sequence, right->sequence));
}

static int
compare_call_audit(const void *left_arg, const void *right_arg)
{
	const struct call_audit *left = left_arg;
	const struct call_audit *right = right_arg;
	int result;

	result = compare_position(left->pos, right->pos);
	if (result != 0)
		return (result);
	return (AVL_CMP(left->sequence, right->sequence));
}

static bool
function_symbol(const struct symbol *symbol)
{
	const struct symbol *type;

	if (symbol == NULL)
		return (false);
	type = symbol->ctype.base_type;
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type != NULL && type->type == SYM_FN);
}

static bool
function_pointer_type(const struct symbol *type)
{
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type == NULL || type->type != SYM_PTR)
		return (false);
	type = type->ctype.base_type;
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type != NULL && type->type == SYM_FN);
}

static void
record_function_escape(struct symbol *symbol, struct position pos)
{
	struct function_escape *escape;

	if (!function_escape_indexes_initialized) {
		avl_create(&function_escapes_by_source,
		    compare_function_escape_source,
		    sizeof (struct function_escape),
		    offsetof(struct function_escape, by_source));
		avl_create(&function_escapes_by_target,
		    compare_function_escape_target,
		    sizeof (struct function_escape),
		    offsetof(struct function_escape, by_target));
		function_escape_indexes_initialized = true;
	}
	escape = calloc(1, sizeof (*escape));
	if (escape == NULL)
		die("out of memory recording function escape");
	escape->tu = function_escape_tu;
	escape->symbol = symbol;
	escape->pos = pos;
	if (++next_function_escape_sequence == 0)
		die("too many function escapes");
	escape->sequence = next_function_escape_sequence;
	avl_add(&function_escapes_by_source, escape);
	*function_escapes_tail = escape;
	function_escapes_tail = &escape->next;
}

static struct function_pointer_activity *
alloc_function_pointer_activity(void)
{
	struct function_pointer_activity *activity;

	activity = calloc(1, sizeof (*activity));
	if (activity == NULL)
		die("out of memory recording function-pointer activity");
	*function_pointer_activities_tail = activity;
	function_pointer_activities_tail = &activity->next;
	return (activity);
}

static void
record_pointer_activity(unsigned int mode, struct position pos,
    struct symbol *symbol, struct symbol *member)
{
	struct function_pointer_activity *activity;
	const struct symbol *type;
	enum function_pointer_activity_kind kind;
	unsigned int read_mode = U_R_VAL | U_R_PTR;
	unsigned int store_mode = U_W_VAL | U_W_PTR;
	unsigned int call_mode = U_CALL | (U_CALL << U_SHIFT);

	if (!record_function_pointer_activity || (mode & call_mode) != 0)
		return;
	type = member != NULL ? member->ctype.base_type :
	    symbol->ctype.base_type;
	if (!function_pointer_type(type))
		return;
	if ((mode & store_mode) != 0) {
		kind = FUNCTION_POINTER_STORE;
	} else if ((mode & read_mode) != 0) {
		kind = FUNCTION_POINTER_LOAD;
	} else {
		return;
	}
	if (!function_pointer_activity_index_initialized) {
		avl_create(&function_pointer_activity_by_source,
		    compare_function_pointer_activity,
		    sizeof (struct function_pointer_activity),
		    offsetof(struct function_pointer_activity, by_source));
		function_pointer_activity_index_initialized = true;
	}
	activity = alloc_function_pointer_activity();
	activity->tu = function_escape_tu;
	activity->symbol = symbol;
	activity->member = member;
	activity->pos = pos;
	activity->kind = kind;
	if (++next_function_pointer_activity_sequence == 0)
		die("too many function-pointer activity records");
	activity->sequence = next_function_pointer_activity_sequence;
	avl_add(&function_pointer_activity_by_source, activity);
}

static void
report_function_pointer_symbol(unsigned int mode, struct position *pos,
    struct symbol *symbol)
{
	if ((mode & U_R_AOF) != 0 && function_symbol(symbol))
		record_function_escape(symbol, *pos);
	record_pointer_activity(mode, *pos, symbol, NULL);
}

static void
report_function_pointer_member(unsigned int mode, struct position *pos,
    struct symbol *symbol, struct symbol *member)
{
	if (member != NULL)
		record_pointer_activity(mode, *pos, symbol, member);
}

static struct function_info *
find_function(struct entrypoint *ep)
{
	struct function_info key = { 0 };

	if (!function_indexes_initialized)
		return (NULL);
	key.ep = ep;
	return (avl_find(&functions_by_entrypoint, &key, NULL));
}

static bool
same_ident(struct ident *left, struct ident *right)
{
	return (compare_ident(left, right) == 0);
}

static struct function_info *
find_identity_function(struct translation_unit *tu, struct symbol *symbol,
    bool internal_linkage)
{
	struct entrypoint entrypoint = { 0 };
	struct function_info key = { 0 };
	struct function_info *function;
	avl_index_t where;

	if (!function_indexes_initialized || symbol->ident == NULL)
		return (NULL);
	entrypoint.name = symbol;
	key.tu = tu;
	key.ep = &entrypoint;
	key.internal_linkage = internal_linkage;
	key.identity_lower_bound = true;
	(void) avl_find(&functions_by_identity, &key, &where);
	function = avl_nearest(&functions_by_identity, where, AVL_AFTER);
	if (function == NULL ||
	    function->internal_linkage != internal_linkage ||
	    (internal_linkage && function->tu != tu) ||
	    !same_ident(function->ep->name->ident, symbol->ident))
		return (NULL);
	return (function);
}

static struct function_info *
find_internal_function(struct translation_unit *tu, struct symbol *symbol)
{
	return (find_identity_function(tu, symbol, true));
}

static struct function_info *
find_external_function(struct symbol *symbol, bool *ambiguous)
{
	struct function_info *function;
	struct function_info *next;

	*ambiguous = false;
	if (symbol->ident == NULL ||
	    (symbol->ctype.modifiers & MOD_STATIC) != 0)
		return (NULL);
	function = find_identity_function(NULL, symbol, false);
	if (function == NULL)
		return (NULL);
	next = AVL_NEXT(&functions_by_identity, function);
	if (next != NULL && !next->internal_linkage &&
	    same_ident(symbol->ident, next->ep->name->ident)) {
		*ambiguous = true;
		return (NULL);
	}
	return (function);
}

static struct function_info *
resolve_function_symbol(struct translation_unit *tu, struct symbol *symbol,
    bool *ambiguous)
{
	struct function_info *function;

	*ambiguous = false;
	function = find_internal_function(tu, symbol);
	/*
	 * Prefer the translation unit's static definition only when it is
	 * visible to this declaration.  An intervening local can make a nested
	 * extern refer to an external function with the same name.
	 */
	if (function != NULL && locklint_symbol_can_use_internal(symbol))
		return (function);
	if (symbol->ep == NULL && symbol->definition != NULL)
		symbol = symbol->definition;
	if (symbol->ep != NULL)
		return (find_function(symbol->ep));
	return (find_external_function(symbol, ambiguous));
}

static void
resolve_function_escapes(void)
{
	struct function_escape *escape;

	for (escape = function_escapes; escape != NULL;
	    escape = escape->next) {
		bool ambiguous;

		escape->target = resolve_function_symbol(escape->tu,
		    escape->symbol, &ambiguous);
		if (escape->target != NULL) {
			escape->target->root_reasons |=
			    FUNCTION_ROOT_POINTER_ESCAPE;
			escape->target->has_exact_escape = true;
			avl_add(&function_escapes_by_target, escape);
		}
	}
}

static struct function_info *
direct_callee(struct function_info *caller, struct instruction *insn)
{
	bool ambiguous;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL)
		return (NULL);
	return (resolve_function_symbol(caller->tu, insn->func->sym,
	    &ambiguous));
}

static bool
ambiguous_external_callee(struct function_info *caller,
    struct instruction *insn)
{
	struct function_info *function;
	bool ambiguous;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL)
		return (false);
	function = resolve_function_symbol(caller->tu, insn->func->sym,
	    &ambiguous);
	return (function == NULL && ambiguous);
}

static struct position
call_position(struct instruction *insn)
{
	return (insn->call_expr != NULL ? insn->call_expr->pos : insn->pos);
}

static const char *
function_name(const struct function_info *function)
{
	struct ident *ident = function->ep->name->ident;

	return (ident != NULL ? show_ident(ident) : "<anonymous>");
}

static void
dump_function_calls(struct function_info *function)
{
	struct call_audit *calls;
	struct basic_block *bb;
	unsigned int count = 0;
	unsigned int index = 0;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			if (insn->bb != NULL && insn->opcode == OP_CALL)
				count++;
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	if (count == 0)
		return;
	calls = calloc(count, sizeof (*calls));
	if (calls == NULL)
		die("out of memory ordering call-graph audit");
	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			if (insn->bb == NULL || insn->opcode != OP_CALL)
				continue;
			calls[index].insn = insn;
			calls[index].pos = call_position(insn);
			calls[index].sequence = index;
			index++;
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	qsort(calls, count, sizeof (*calls), compare_call_audit);
	for (index = 0; index < count; index++) {
		struct instruction *insn = calls[index].insn;
		struct function_info *callee = direct_callee(function, insn);
		struct symbol *symbol = NULL;

		(void) printf("  call %s:%u:%u ",
		    stream_name(calls[index].pos.stream),
		    calls[index].pos.line, calls[index].pos.pos);
		if (callee != NULL) {
			(void) printf("resolved %s tu=%s\n",
			    function_name(callee),
			    locklint_translation_unit_file(callee->tu));
			continue;
		}
		if (ambiguous_external_callee(function, insn)) {
			(void) printf("ambiguous-external\n");
			continue;
		}
		if (insn->func != NULL && insn->func->type == PSEUDO_SYM)
			symbol = insn->func->sym;
		if (function_symbol(symbol)) {
			(void) printf("unresolved-external %s\n",
			    symbol->ident != NULL ?
			    show_ident(symbol->ident) : "<anonymous>");
		} else {
			(void) printf("indirect\n");
		}
	}
	free(calls);
}

static void
dump_callgraph(void)
{
	struct function_escape *escape;
	struct function_pointer_activity *activity;
	struct function_info *function;

	for (function = functions; function != NULL; function = function->next) {
		struct symbol *definition = function->ep->name;

		(void) printf("function %s tu=%s definition=%s:%u:%u "
		    "linkage=%s reachable=%s\n", function_name(function),
		    locklint_translation_unit_file(function->tu),
		    stream_name(definition->pos.stream), definition->pos.line,
		    definition->pos.pos,
		    function->internal_linkage ? "internal" : "external",
		    function->reachable_from_root ? "yes" : "no");
		if ((function->root_reasons & FUNCTION_ROOT_EXTERNAL) != 0)
			(void) printf("  root external-linkage\n");
		if ((function->root_reasons &
		    FUNCTION_ROOT_NO_DIRECT_CALLER) != 0)
			(void) printf("  root no-known-direct-caller\n");
		if ((function->root_reasons &
		    FUNCTION_ROOT_POINTER_ESCAPE) != 0) {
			(void) printf("  root function-pointer-escape");
			if (!function->has_exact_escape) {
				(void) printf(" fallback=%s:%u:%u",
				    stream_name(definition->pos.stream),
				    definition->pos.line, definition->pos.pos);
			}
			(void) printf("\n");
		}
		dump_function_calls(function);
	}
	(void) printf("escapes\n");
	for (escape = function_escape_indexes_initialized ?
	    avl_first(&function_escapes_by_source) : NULL;
	    escape != NULL;
	    escape = AVL_NEXT(&function_escapes_by_source, escape)) {
		(void) printf("  escape %s:%u:%u %s ",
		    stream_name(escape->pos.stream), escape->pos.line,
		    escape->pos.pos,
		    escape->symbol->ident != NULL ?
		    show_ident(escape->symbol->ident) : "<anonymous>");
		if (escape->target != NULL) {
			(void) printf("resolved %s tu=%s\n",
			    function_name(escape->target),
			    locklint_translation_unit_file(escape->target->tu));
		} else {
			(void) printf("unresolved\n");
		}
	}
	(void) printf("pointer activity\n");
	for (activity = function_pointer_activity_index_initialized ?
	    avl_first(&function_pointer_activity_by_source) : NULL;
	    activity != NULL;
	    activity = AVL_NEXT(&function_pointer_activity_by_source,
	    activity)) {
		const char *kind;

		switch (activity->kind) {
		case FUNCTION_POINTER_LOAD:
			kind = "load";
			break;
		case FUNCTION_POINTER_STORE:
			kind = "store";
			break;
		default:
			abort();
		}
		(void) printf("  %s %s:%u:%u ", kind,
		    stream_name(activity->pos.stream), activity->pos.line,
		    activity->pos.pos);
		if (activity->member != NULL) {
			(void) printf("%s.%s\n",
			    activity->symbol->ident != NULL ?
			    show_ident(activity->symbol->ident) : "<anonymous>",
			    activity->member->ident != NULL ?
			    show_ident(activity->member->ident) : "<anonymous>");
		} else {
			(void) printf("%s\n",
			    activity->symbol->ident != NULL ?
			    show_ident(activity->symbol->ident) : "<anonymous>");
		}
	}
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
argument_lock(struct translation_unit *tu, struct expression *argument,
    struct symbol *member, unsigned long offset, struct locklint_access *lock)
{
	if (!locklint_get_access(tu, argument, lock))
		return (false);
	lock->member = argument_member(argument, member);
	lock->offset += offset;
	lock->path = NULL;
	return (true);
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
	struct locklint_access base;
	struct expression *argument;

	if (condition->data_root == NULL) {
		argument = call_argument(insn, condition->argument);
		if (!locklint_get_access(function->tu, argument, &base))
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
	if (!condition->has_lock) {
		*lock = (struct locklint_access){ 0 };
	} else if (condition->lock_root == NULL) {
		/* Relative locks move with the selected data object's base. */
		*lock = base;
		lock->member = argument_member(argument,
		    condition->lock_member);
		lock->offset += condition->lock_offset;
		lock->path = NULL;
	} else {
		/* Absolute locks keep their original program-wide identity. */
		lock->root = condition->lock_root;
		lock->object = condition->lock_object;
		lock->type = condition->lock_root->ctype.base_type;
		lock->member = condition->lock_member;
		lock->offset = condition->lock_offset;
		lock->expr = NULL;
		lock->path = NULL;
	}
	*object = base;
	object->member = argument_member(argument, condition->data_member);
	object->offset += condition->data_offset;
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
add_transfer(struct function_info *function, unsigned int argument,
    struct symbol *lock_member, unsigned long lock_offset, bool *added)
{
	struct lock_transfer *transfer;
	unsigned int state;

	for (transfer = function->transfers; transfer != NULL;
	    transfer = transfer->next) {
		if (transfer->argument == argument &&
		    transfer->lock_member == lock_member &&
		    transfer->lock_offset == lock_offset) {
			*added = false;
			return (transfer);
		}
	}
	transfer = calloc(1, sizeof (*transfer));
	if (transfer == NULL)
		die("out of memory recording lock transfer");
	transfer->argument = argument;
	transfer->lock_member = lock_member;
	transfer->lock_offset = lock_offset;
	for (state = 0; state < LOCK_STATE_COUNT; state++)
		transfer->output[state] = state;
	transfer->next = function->transfers;
	function->transfers = transfer;
	*added = true;
	return (transfer);
}

static void
transfer_call_effects(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	struct function_info *callee = direct_callee(function, insn);
	struct lock_transfer *transfer;

	if (callee == NULL)
		return;
	for (transfer = callee->transfers; transfer != NULL;
	    transfer = transfer->next) {
		struct locklint_access lock;
		struct expression *argument;
		lock_state_t state;

		argument = call_argument(insn, transfer->argument);
		if (!argument_lock(function->tu, argument, transfer->lock_member,
		    transfer->lock_offset, &lock))
			continue;
		state = get_state(*states, &lock);
		set_state(states, &lock, transfer->output[state]);
	}
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
add_protection_condition(struct function_info *function,
    unsigned int argument, struct symbol *data_root,
    struct object_identity *data_object,
    bool has_lock, unsigned int required_modes,
    struct symbol *lock_root, struct object_identity *lock_object,
    struct symbol *lock_member, unsigned long lock_offset,
    struct symbol *data_member, unsigned long data_offset,
    struct position pos)
{
	struct protection_condition *condition;

	for (condition = function->conditions; condition != NULL;
	    condition = condition->next) {
		if (condition->argument == argument &&
		    same_condition_data(condition, data_root, data_object,
		    data_member, data_offset) &&
		    same_condition_lock(condition, has_lock, required_modes,
		    lock_root,
		    lock_object, lock_member, lock_offset))
			return (false);
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
	condition->next = function->conditions;
	function->conditions = condition;
	return (true);
}

static bool
defer_conditions(struct function_info *function)
{
	return (function->reachable_from_root && function->root_reasons == 0);
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
	struct locklint_access base;
	struct locklint_access relative;
	struct expression *argument;

	if (transfer->data_root == NULL) {
		argument = call_argument(insn, transfer->argument);
		if (!locklint_get_access(function->tu, argument, &base))
			return (false);
		relative = (struct locklint_access){ 0 };
		relative.member = transfer->data_member;
		relative.offset = transfer->data_offset;
		relative.path = transfer->data_path;
		locklint_rebase_access(&base, &relative, region);
	} else {
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
	struct function_info *callee = direct_callee(function, insn);
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
	    state->competition == COMPETITION_NONE)
		return (PROTECTION_DEFINITE);
	if (lock_partial ||
	    visibility == VISIBILITY_MAYBE ||
	    state->competition_path_dependent)
		return (PROTECTION_PATH_DEPENDENT);
	return (PROTECTION_ABSENT);
}

static bool
protected_access(struct function_info *function, struct instruction *insn,
    struct locklint_access *access, struct locklint_access *lock,
    struct symbol **data_member, unsigned int *required_modes)
{
	struct locklint_data_policy policy;

	if (insn->access == NULL ||
	    (insn->opcode != OP_LOAD && insn->opcode != OP_STORE) ||
	    !locklint_get_access(function->tu, insn->access, access))
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

static void
mark_reachable(struct function_info *function)
{
	struct basic_block *bb;

	if (function->reachable_from_root)
		return;
	function->reachable_from_root = true;
	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct function_info *callee;

			if (insn->bb == NULL)
				continue;
			callee = direct_callee(function, insn);
			if (callee != NULL)
				mark_reachable(callee);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

/*
 * First account for every non-self direct incoming edge, then assign all
 * applicable root reasons and propagate reachability from the resulting
 * roots.
 */
static void
classify_roots(void)
{
	struct function_info *function;

	for (function = functions; function != NULL; function = function->next) {
		struct basic_block *bb;

		FOR_EACH_PTR(function->ep->bbs, bb) {
			struct instruction *insn;

			FOR_EACH_PTR(bb->insns, insn) {
				struct function_info *callee;

				if (insn->bb == NULL)
					continue;
				callee = direct_callee(function, insn);
				if (callee != NULL && callee != function)
					callee->has_nonself_direct_caller = true;
			} END_FOR_EACH_PTR(insn);
		} END_FOR_EACH_PTR(bb);
	}
	for (function = functions; function != NULL; function = function->next) {
		unsigned long modifiers =
		    function->ep->name->ctype.modifiers;

		if (!function->internal_linkage)
			function->root_reasons |= FUNCTION_ROOT_EXTERNAL;
		if (!function->has_nonself_direct_caller)
			function->root_reasons |=
			    FUNCTION_ROOT_NO_DIRECT_CALLER;
		/*
		 * Sparse marks ordinary external definitions addressable.
		 * They are already roots, so use this fallback only where it
		 * adds conservative information for an internal function.
		 */
		if (function->internal_linkage &&
		    (modifiers & MOD_ADDRESSABLE) != 0)
			function->root_reasons |=
			    FUNCTION_ROOT_POINTER_ESCAPE;
	}
	for (function = functions; function != NULL; function = function->next) {
		if (function->root_reasons != 0)
			mark_reachable(function);
	}
}

static bool
collect_local_transfers(struct function_info *function)
{
	struct basic_block *bb;
	bool changed = false;

	FOR_EACH_PTR(function->ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct locklint_access lock;
			enum locklint_lock_action action;
			enum locklint_lock_mode mode;
			unsigned int argument;
			bool added;

			if (insn->bb == NULL)
				continue;
			action = locklint_get_lock_action(function->tu, insn,
			    &lock, &mode);
			if (action == LOCKLINT_LOCK_NONE || lock.root == NULL ||
			    !formal_argument(function->ep, lock.root, &argument))
				continue;
			(void) add_transfer(function, argument, lock.member,
			    lock.offset, &added);
			if (added)
				changed = true;
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	return (changed);
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
			callee = direct_callee(function, insn);
			if (callee == NULL)
				continue;
			for (transfer = callee->transfers; transfer != NULL;
			    transfer = transfer->next) {
				struct locklint_access access;
				struct expression *actual;
				unsigned int argument;
				bool added;

				actual = call_argument(insn, transfer->argument);
				if (!locklint_get_access(function->tu, actual,
				    &access) ||
				    !formal_argument(function->ep, access.root,
				    &argument))
					continue;
				(void) add_transfer(function, argument,
				    argument_member(actual,
				    transfer->lock_member),
				    access.offset + transfer->lock_offset, &added);
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

static void
simulate_instruction(struct function_info *function,
    struct locklint_access *target, lock_state_t *state,
    unsigned int *invalid, struct instruction *insn)
{
	struct function_info *callee;
	struct lock_transfer *transfer;
	struct locklint_access lock;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;

	callee = direct_callee(function, insn);
	if (callee != NULL) {
		for (transfer = callee->transfers; transfer != NULL;
		    transfer = transfer->next) {
			struct expression *actual;

			actual = call_argument(insn, transfer->argument);
			if (!argument_lock(function->tu, actual,
			    transfer->lock_member,
			    transfer->lock_offset, &lock))
				continue;
			if (!same_lock(target, &lock))
				continue;
			*invalid |= transfer->invalid[*state];
			*state = transfer->output[*state];
		}
	}

	action = locklint_get_lock_action(function->tu, insn, &lock, &mode);
	if (action == LOCKLINT_LOCK_NONE || !same_lock(target, &lock))
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
    struct locklint_access *target, lock_state_t in,
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
	struct transfer_block_info *blocks = NULL;
	struct transfer_block_info **tail = &blocks;
	struct transfer_block_info *block;
	struct basic_block *bb;
	struct locklint_access target;
	lock_state_t result = input;
	unsigned int result_invalid = 0;
	bool found_return = false;
	bool changed;

	/*
	 * A transfer target is relative to a formal argument, which has no C
	 * linkage and therefore deliberately has no canonical object identity.
	 */
	target.root = formal_symbol(function->ep, transfer->argument);
	target.object = NULL;
	target.type = NULL;
	target.member = transfer->lock_member;
	target.offset = transfer->lock_offset;
	target.expr = NULL;
	target.path = NULL;
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
			simulate_block(function, block, &target, in, invalid_in,
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
			result_invalid = block->invalid_out;
			found_return = true;
		} else {
			result = merge_lock_state(result, block->out);
			result_invalid |= block->invalid_out;
		}
	}
	while (blocks != NULL) {
		struct transfer_block_info *next = blocks->next;

		free(blocks);
		blocks = next;
	}
	*invalid = result_invalid;
	return (result);
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
			callee = direct_callee(function, insn);
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

	callee = direct_callee(function, insn);
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
		    access->member, access->offset, assumption->pos);
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
				(void) add_protection_condition(function, argument,
				    data_root, data_object,
				    true, required_modes,
				    lock.root != access.root ? lock.root : NULL,
				    lock.root != access.root ?
				    lock.object : NULL,
				    lock.member, lock.offset, data_member,
				    access.offset, insn->access->pos);
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
	struct function_info *callee = direct_callee(function, insn);
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

		if (!map_call_protection_condition(function, insn, condition,
		    &object, &lock))
			continue;
		status = get_protection_status(function, state, &object,
		    condition->required_modes, &lock);
		if (status != PROTECTION_ABSENT ||
		    !condition_data_identity(function, &object,
		    &caller_argument, &data_root, &data_object))
			continue;
		if (add_protection_condition(function, caller_argument,
		    data_root, data_object,
		    condition->has_lock, condition->required_modes,
		    condition->lock_root, condition->lock_object,
		    lock.member, lock.offset, object.member, object.offset,
		    condition->pos))
			changed = true;
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

	if (insn->opcode != OP_STORE || insn->access == NULL ||
	    !locklint_get_access(function->tu, insn->access, &access) ||
	    !locklint_data_policy(&access, &policy, &lock) ||
	    !policy.read_only)
		return;
	visibility = get_visibility(state->visibility, &access);
	if (state->competition == COMPETITION_NONE ||
	    visibility == VISIBILITY_INVISIBLE)
		return;
	data = access.member != NULL ? access.member : access.root;
	name = data != NULL && data->ident != NULL ?
	    show_ident(data->ident) : "<unknown>";
	if (state->competition == COMPETITION_PRESENT &&
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

static void
check_direct_call(struct function_info *function,
    struct analysis_state *analysis, struct instruction *insn)
{
	struct function_info *callee = direct_callee(function, insn);
	struct protection_condition *condition;
	struct position pos;

	if (callee == NULL) {
		if (ambiguous_external_callee(function, insn)) {
			pos = insn->call_expr != NULL ?
			    insn->call_expr->pos : insn->pos;
			warning(pos, "locklint: direct call has multiple "
			    "external definitions");
		}
		return;
	}
	pos = insn->call_expr != NULL ? insn->call_expr->pos : insn->pos;
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
		struct lock_transfer *transfer;

		for (transfer = callee->transfers; transfer != NULL;
		    transfer = transfer->next) {
			struct locklint_access lock;
			struct expression *argument;
			lock_state_t state;

			argument = call_argument(insn, transfer->argument);
			if (!argument_lock(function->tu, argument,
			    transfer->lock_member, transfer->lock_offset,
			    &lock))
				continue;
			state = get_state(analysis->locks, &lock);
			if (transfer->invalid[state] & INVALID_ACQUIRE) {
				warning(pos, "locklint: call to '%s' may acquire "
				    "already-held lock '%s'",
				    show_ident(callee->ep->name->ident),
				    lock_name(&lock));
			}
			if (transfer->invalid[state] & INVALID_RELEASE) {
				warning(pos, "locklint: call to '%s' may release "
				    "lock '%s' that is not held",
				    show_ident(callee->ep->name->ident),
				    lock_name(&lock));
			}
			set_state(&analysis->locks, &lock,
			    transfer->output[state]);
		}
	}
	transfer_call_visibility_effects(function, &analysis->visibility, insn);
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
	unsigned int argument;
	bool defer;

	action = locklint_get_lock_action(function->tu, insn, &lock, &mode);
	if (action == LOCKLINT_LOCK_NONE || lock.root == NULL)
		return;
	state = get_state(analysis->locks, &lock);
	defer = defer_conditions(function) &&
	    formal_argument(function->ep, lock.root, &argument);
	pos = insn->call_expr != NULL ? insn->call_expr->pos : insn->pos;
	if (action == LOCKLINT_LOCK_ACQUIRE) {
		if (state_definitely_held(state) && !defer) {
			warning(pos, "locklint: lock '%s' is already held",
			    lock_name(&lock));
		} else if (state_maybe_held(state) && !defer) {
			warning(pos, "locklint: lock '%s' may already be held",
			    lock_name(&lock));
		}
		set_state(&analysis->locks, &lock, mode);
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
			check_direct_call(function, state, insn);
			check_lock_action(function, state, insn);
			transfer_assertion(function, &state->locks, insn);
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

static void
free_function_escapes(void)
{
	while (function_escapes != NULL) {
		struct function_escape *next = function_escapes->next;

		avl_remove(&function_escapes_by_source, function_escapes);
		if (function_escapes->target != NULL)
			avl_remove(&function_escapes_by_target, function_escapes);
		free(function_escapes);
		function_escapes = next;
	}
	if (function_escape_indexes_initialized) {
		avl_destroy(&function_escapes_by_source);
		avl_destroy(&function_escapes_by_target);
		function_escape_indexes_initialized = false;
	}
	next_function_escape_sequence = 0;
	function_escapes_tail = &function_escapes;
}

static void
free_function_pointer_activity(void)
{
	while (function_pointer_activities != NULL) {
		struct function_pointer_activity *next =
		    function_pointer_activities->next;

		avl_remove(&function_pointer_activity_by_source,
		    function_pointer_activities);
		free(function_pointer_activities);
		function_pointer_activities = next;
	}
	if (function_pointer_activity_index_initialized) {
		avl_destroy(&function_pointer_activity_by_source);
		function_pointer_activity_index_initialized = false;
	}
	function_pointer_activities_tail = &function_pointer_activities;
	next_function_pointer_activity_sequence = 0;
}

void
locklint_check_record_pointer_evidence(struct translation_unit *tu,
    struct symbol_list *symbols, bool record_activity)
{
	static struct reporter reporter = {
		.r_symbol = report_function_pointer_symbol,
		.r_member = report_function_pointer_member,
	};

	function_escape_tu = tu;
	record_function_pointer_activity = record_activity;
	dissect(symbols, &reporter);
	record_function_pointer_activity = false;
	function_escape_tu = NULL;
}

void
locklint_check_add(struct translation_unit *tu, struct entrypoint *ep)
{
	struct function_info *function;

	if (!function_indexes_initialized) {
		avl_create(&functions_by_entrypoint, compare_function_entrypoint,
		    sizeof (struct function_info),
		    offsetof(struct function_info, by_entrypoint));
		avl_create(&functions_by_identity, compare_function_identity,
		    sizeof (struct function_info),
		    offsetof(struct function_info, by_identity));
		function_indexes_initialized = true;
	}
	function = calloc(1, sizeof (*function));
	if (function == NULL)
		die("out of memory registering function analysis");
	function->tu = tu;
	function->ep = ep;
	function->internal_linkage =
	    (ep->name->ctype.modifiers & MOD_STATIC) != 0;
	if (++next_function_identity_sequence == 0)
		die("too many functions for identity index");
	function->identity_sequence = next_function_identity_sequence;
	avl_add(&functions_by_entrypoint, function);
	avl_add(&functions_by_identity, function);
	*functions_tail = function;
	functions_tail = &function->next;
}

/*
 * Stabilize transfer summaries before block state, then stabilize caller
 * protection conditions before replaying the final states to emit diagnostics.
 */
static void
run_lock_checks(void)
{
	struct function_info *function;
	bool changed;

	for (function = functions; function != NULL; function = function->next) {
		(void) collect_local_transfers(function);
		(void) collect_local_visibility_transfers(function);
	}
	do {
		changed = false;
		for (function = functions; function != NULL;
		    function = function->next) {
			if (propagate_transfer_candidates(function))
				changed = true;
			if (propagate_visibility_transfer_candidates(function))
				changed = true;
		}
		for (function = functions; function != NULL;
		    function = function->next) {
			if (solve_function_transfers(function))
				changed = true;
			if (solve_function_visibility_transfers(function))
				changed = true;
		}
	} while (changed);
	for (function = functions; function != NULL; function = function->next)
		collect_function_assumptions(function);
	for (function = functions; function != NULL; function = function->next)
		function->blocks = analyze_blocks(function);
	for (function = functions; function != NULL; function = function->next)
		collect_local_protection_conditions(function);
	do {
		changed = false;
		for (function = functions; function != NULL;
		    function = function->next) {
			if (propagate_function_protection_conditions(function))
				changed = true;
		}
	} while (changed);
	for (function = functions; function != NULL; function = function->next)
		emit_diagnostics(function);
}

static void
free_functions(void)
{
	while (functions != NULL) {
		struct function_info *next = functions->next;
		struct protection_condition *condition = functions->conditions;
		struct assumed_region *assumption = functions->assumptions;
		struct lock_transfer *transfer = functions->transfers;
		struct visibility_transfer *visibility_transfer =
		    functions->visibility_transfers;

		while (condition != NULL) {
			struct protection_condition *condition_next =
			    condition->next;

			free(condition);
			condition = condition_next;
		}
		while (assumption != NULL) {
			struct assumed_region *assumption_next = assumption->next;

			free(assumption);
			assumption = assumption_next;
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
		avl_remove(&functions_by_entrypoint, functions);
		avl_remove(&functions_by_identity, functions);
		free_blocks(functions->blocks);
		free(functions);
		functions = next;
	}
	if (function_indexes_initialized) {
		avl_destroy(&functions_by_entrypoint);
		avl_destroy(&functions_by_identity);
		function_indexes_initialized = false;
	}
	next_function_identity_sequence = 0;
	functions_tail = &functions;
}

/*
 * Complete module-wide resolution and root classification, run the requested
 * audit and checks, then release all checker-owned analysis records.
 */
void
locklint_check_all(bool check_locks, bool show_callgraph)
{
	resolve_function_escapes();
	classify_roots();
	if (show_callgraph)
		dump_callgraph();
	if (check_locks)
		run_lock_checks();
	free_function_pointer_activity();
	free_function_escapes();
	free_functions();
}
