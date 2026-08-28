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
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "avl.h"
#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "annotations.h"
#include "assertions.h"
#include "check.h"
#include "events.h"
#include "identity.h"
#include "symbol.h"

enum lock_state {
	LOCK_NOT_HELD,
	LOCK_HELD,
	LOCK_MAYBE_HELD,
	LOCK_STATE_COUNT
};

#define	INVALID_ACQUIRE	0x1
#define	INVALID_RELEASE	0x2

struct state_entry {
	struct locklint_access lock;
	enum lock_state state;
	bool side_effect;
	struct state_entry *next;
};

struct block_info {
	struct basic_block *bb;
	bool reachable;
	struct state_entry *in;
	struct state_entry *out;
	struct block_info *next;
};

struct transfer_block_info {
	struct basic_block *bb;
	bool reachable;
	enum lock_state in;
	enum lock_state out;
	unsigned int invalid_in;
	unsigned int invalid_out;
	struct transfer_block_info *next;
};

struct lock_condition {
	unsigned int argument;
	/*
	 * A NULL root is relative to argument.  An absolute root retains both
	 * its source symbol and canonical identity across translation units.
	 */
	struct symbol *lock_root;
	struct object_identity *lock_object;
	struct symbol *lock_member;
	unsigned long lock_offset;
	struct symbol *data_member;
	struct position pos;
	struct lock_condition *next;
};

struct lock_transfer {
	unsigned int argument;
	struct symbol *lock_member;
	unsigned long lock_offset;
	enum lock_state output[LOCK_STATE_COUNT];
	unsigned int invalid[LOCK_STATE_COUNT];
	struct lock_transfer *next;
};

struct function_info {
	/* Functions and their retained Sparse objects belong to one parse. */
	struct translation_unit *tu;
	struct entrypoint *ep;
	struct block_info *blocks;
	struct lock_condition *conditions;
	struct lock_transfer *transfers;
	bool has_direct_caller;
	bool reachable_from_root;
	avl_node_t by_entrypoint;
	struct function_info *next;
};

static struct function_info *functions;
static struct function_info **functions_tail = &functions;
static avl_tree_t functions_by_entrypoint;
static bool functions_by_entrypoint_initialized;

static void transfer_call_effects(struct function_info *,
    struct state_entry **, struct instruction *);

static bool
same_lock(const struct locklint_access *left,
    const struct locklint_access *right)
{
	return (locklint_same_access(left, right));
}

static struct state_entry *
alloc_state(const struct locklint_access *lock, enum lock_state state)
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

static enum lock_state
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
    enum lock_state state)
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
    const struct locklint_access *lock, enum lock_state state)
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
		if (get_state(incoming, &entry->lock) != entry->state)
			entry->state = LOCK_MAYBE_HELD;
	}
	for (entry = incoming; entry != NULL; entry = entry->next) {
		if (get_state(*merged, &entry->lock) == LOCK_NOT_HELD) {
			set_state(merged, &entry->lock, LOCK_MAYBE_HELD);
			(*merged)->side_effect = entry->side_effect;
		}
	}
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

	action = locklint_get_lock_action(function->tu, insn, &lock);
	if (action == LOCKLINT_LOCK_ACQUIRE && lock.root != NULL)
		set_state(states, &lock, LOCK_HELD);
	else if (action == LOCKLINT_LOCK_RELEASE && lock.root != NULL)
		set_state(states, &lock, LOCK_NOT_HELD);
}

static void
transfer_assertion(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	struct locklint_access lock;
	enum locklint_assertion assertion;

	assertion = locklint_get_assertion(function->tu, insn, &lock);
	if (assertion == LOCKLINT_ASSERT_HELD)
		set_asserted_state(states, &lock, LOCK_HELD);
	else if (assertion == LOCKLINT_ASSERT_NOT_HELD)
		set_asserted_state(states, &lock, LOCK_NOT_HELD);
}

static void
transfer_instruction(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	transfer_call_effects(function, states, insn);
	transfer_lock_action(function, states, insn);
	transfer_assertion(function, states, insn);
}

static struct state_entry *
transfer_block(struct function_info *function, struct basic_block *bb,
    struct state_entry *in)
{
	struct state_entry *out = copy_states(in);
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (insn->bb == NULL)
			continue;
		transfer_instruction(function, &out, insn);
	} END_FOR_EACH_PTR(insn);

	return (out);
}

static struct state_entry *
merge_parents(struct block_info *blocks, struct basic_block *bb,
    bool *reachable)
{
	struct state_entry *merged = NULL;
	struct basic_block *parent;
	bool first = true;

	*reachable = false;
	FOR_EACH_PTR(bb->parents, parent) {
		struct block_info *block = find_block(blocks, parent);

		if (block == NULL || !block->reachable)
			continue;
		if (first) {
			merged = copy_states(block->out);
			first = false;
		} else {
			merge_states(&merged, block->out);
		}
		*reachable = true;
	} END_FOR_EACH_PTR(parent);
	return (merged);
}

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
			struct state_entry *in;
			struct state_entry *out;
			bool reachable;

			if (block->bb == ep->entry->bb) {
				in = NULL;
				reachable = true;
			} else {
				in = merge_parents(blocks, block->bb, &reachable);
			}
			if (!reachable) {
				free_states(in);
				continue;
			}
			out = transfer_block(function, block->bb, in);
			if (!block->reachable ||
			    !same_states(block->in, in) ||
			    !same_states(block->out, out)) {
				changed = true;
			}
			free_states(block->in);
			free_states(block->out);
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

static struct function_info *
find_function(struct entrypoint *ep)
{
	struct function_info key = { 0 };

	if (!functions_by_entrypoint_initialized)
		return (NULL);
	key.ep = ep;
	return (avl_find(&functions_by_entrypoint, &key, NULL));
}

static bool
same_ident(struct ident *left, struct ident *right)
{
	return (left == right ||
	    (left != NULL && right != NULL &&
	    left->len == right->len &&
	    memcmp(left->name, right->name, left->len) == 0));
}

static struct function_info *
find_internal_function(struct translation_unit *tu, struct ident *ident)
{
	struct function_info *function;

	for (function = functions; function != NULL; function = function->next) {
		struct symbol *definition = function->ep->name;

		if (function->tu == tu &&
		    (definition->ctype.modifiers & MOD_STATIC) != 0 &&
		    same_ident(ident, definition->ident))
			return (function);
	}
	return (NULL);
}

static struct function_info *
find_external_function(struct symbol *symbol, bool *ambiguous)
{
	struct function_info *function;
	struct function_info *found = NULL;

	*ambiguous = false;
	if (symbol->ident == NULL ||
	    (symbol->ctype.modifiers & MOD_STATIC) != 0)
		return (NULL);
	for (function = functions; function != NULL; function = function->next) {
		struct symbol *definition = function->ep->name;

		if ((definition->ctype.modifiers & MOD_STATIC) != 0 ||
		    !same_ident(symbol->ident, definition->ident))
			continue;
		if (found != NULL) {
			*ambiguous = true;
			return (NULL);
		}
		found = function;
	}
	return (found);
}

static struct function_info *
direct_callee(struct function_info *caller, struct instruction *insn)
{
	struct function_info *function;
	struct symbol *symbol;
	bool ambiguous;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL)
		return (NULL);
	symbol = insn->func->sym;
	function = find_internal_function(caller->tu, symbol->ident);
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
	return (find_external_function(symbol, &ambiguous));
}

static bool
ambiguous_external_callee(struct function_info *caller,
    struct instruction *insn)
{
	struct symbol *symbol;
	bool ambiguous;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL)
		return (false);
	symbol = insn->func->sym;
	if (find_internal_function(caller->tu, symbol->ident) != NULL &&
	    locklint_symbol_can_use_internal(symbol))
		return (false);
	if (symbol->ep != NULL ||
	    (symbol->definition != NULL && symbol->definition->ep != NULL))
		return (false);
	(void) find_external_function(symbol, &ambiguous);
	return (ambiguous);
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
	return (true);
}

static bool
map_call_lock_condition(struct function_info *function,
    struct instruction *insn, const struct lock_condition *condition,
    struct locklint_access *object, struct locklint_access *lock)
{
	struct expression *argument;

	argument = call_argument(insn, condition->argument);
	if (!locklint_get_access(function->tu, argument, object))
		return (false);
	if (condition->lock_root == NULL) {
		/* Relative locks move with the formal data object. */
		*lock = *object;
		lock->member = argument_member(argument,
		    condition->lock_member);
		lock->offset += condition->lock_offset;
	} else {
		/* Absolute locks keep their original program-wide identity. */
		lock->root = condition->lock_root;
		lock->object = condition->lock_object;
		lock->type = condition->lock_root->ctype.base_type;
		lock->member = condition->lock_member;
		lock->offset = condition->lock_offset;
		lock->expr = NULL;
	}
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
		enum lock_state state;

		argument = call_argument(insn, transfer->argument);
		if (!argument_lock(function->tu, argument, transfer->lock_member,
		    transfer->lock_offset, &lock))
			continue;
		state = get_state(*states, &lock);
		set_state(states, &lock, transfer->output[state]);
	}
}

static bool
same_condition_lock(const struct lock_condition *condition,
    struct symbol *root, struct object_identity *object,
    struct symbol *member, unsigned long offset)
{
	struct locklint_access left = { 0 };
	struct locklint_access right = { 0 };

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
add_lock_condition(struct function_info *function, unsigned int argument,
    struct symbol *lock_root, struct object_identity *lock_object,
    struct symbol *lock_member, unsigned long lock_offset,
    struct symbol *data_member, struct position pos)
{
	struct lock_condition *condition;

	for (condition = function->conditions; condition != NULL;
	    condition = condition->next) {
		if (condition->argument == argument &&
		    same_condition_lock(condition, lock_root, lock_object,
		    lock_member, lock_offset) &&
		    same_ident(condition->data_member == NULL ? NULL :
		    condition->data_member->ident,
		    data_member == NULL ? NULL : data_member->ident))
			return (false);
	}
	condition = calloc(1, sizeof (*condition));
	if (condition == NULL)
		die("out of memory recording lock condition");
	condition->argument = argument;
	condition->lock_root = lock_root;
	condition->lock_object = lock_object;
	condition->lock_member = lock_member;
	condition->lock_offset = lock_offset;
	condition->data_member = data_member;
	condition->pos = pos;
	condition->next = function->conditions;
	function->conditions = condition;
	return (true);
}

static bool
defer_formal_lock_conditions(struct function_info *function)
{
	unsigned long modifiers = function->ep->name->ctype.modifiers;

	return (function->reachable_from_root && function->has_direct_caller &&
	    (modifiers & MOD_STATIC) != 0 &&
	    (modifiers & MOD_ADDRESSABLE) == 0);
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

static bool
protected_access(struct function_info *function, struct instruction *insn,
    struct locklint_access *access, struct locklint_access *lock,
    struct symbol **data_member)
{
	if (insn->access == NULL ||
	    (insn->opcode != OP_LOAD && insn->opcode != OP_STORE) ||
	    !locklint_get_access(function->tu, insn->access, access))
		return (false);
	if (!locklint_protecting_access(access, lock))
		return (false);
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

static void
mark_direct_callers(void)
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
					callee->has_direct_caller = true;
			} END_FOR_EACH_PTR(insn);
		} END_FOR_EACH_PTR(bb);
	}
	for (function = functions; function != NULL; function = function->next) {
		if (!function->has_direct_caller ||
		    (function->ep->name->ctype.modifiers & MOD_STATIC) == 0)
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
			unsigned int argument;
			bool added;

			if (insn->bb == NULL)
				continue;
			action = locklint_get_lock_action(function->tu, insn,
			    &lock);
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

static enum lock_state
merge_lock_state(enum lock_state left, enum lock_state right)
{
	return (left == right ? left : LOCK_MAYBE_HELD);
}

static void
simulate_instruction(struct function_info *function,
    struct locklint_access *target, enum lock_state *state,
    unsigned int *invalid, struct instruction *insn)
{
	struct function_info *callee;
	struct lock_transfer *transfer;
	struct locklint_access lock;
	enum locklint_lock_action action;

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

	action = locklint_get_lock_action(function->tu, insn, &lock);
	if (action == LOCKLINT_LOCK_NONE || !same_lock(target, &lock))
		return;
	if (action == LOCKLINT_LOCK_ACQUIRE) {
		if (*state != LOCK_NOT_HELD)
			*invalid |= INVALID_ACQUIRE;
		*state = LOCK_HELD;
	} else {
		if (*state != LOCK_HELD)
			*invalid |= INVALID_RELEASE;
		*state = LOCK_NOT_HELD;
	}
}

static void
simulate_block(struct function_info *function,
    struct transfer_block_info *block,
    struct locklint_access *target, enum lock_state in,
    unsigned int invalid_in, enum lock_state *out,
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

static enum lock_state
simulate_transfer(struct function_info *function,
    struct lock_transfer *transfer, enum lock_state input,
    unsigned int *invalid)
{
	struct transfer_block_info *blocks = NULL;
	struct transfer_block_info **tail = &blocks;
	struct transfer_block_info *block;
	struct basic_block *bb;
	struct locklint_access target;
	enum lock_state result = input;
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
			enum lock_state in = input;
			unsigned int invalid_in = 0;
			struct basic_block *parent;
			bool reachable = block->bb == function->ep->entry->bb;
			bool first = true;
			enum lock_state out;
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

		for (input = 0; input < LOCK_STATE_COUNT; input++) {
			enum lock_state output;
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
collect_local_lock_conditions(struct function_info *function)
{
	struct block_info *block;

	for (block = function->blocks; block != NULL; block = block->next) {
		struct state_entry *states;
		struct instruction *insn;

		if (!block->reachable)
			continue;
		states = copy_states(block->in);
		FOR_EACH_PTR(block->bb->insns, insn) {
			struct locklint_access access;
			struct locklint_access lock;
			struct symbol *data_member;
			unsigned int argument;

			if (insn->bb == NULL)
				continue;
			if (protected_access(function, insn, &access, &lock,
			    &data_member) &&
			    get_state(states, &lock) == LOCK_NOT_HELD &&
			    formal_argument(function->ep, access.root,
			    &argument)) {
				(void) add_lock_condition(function, argument,
				    lock.root != access.root ? lock.root : NULL,
				    lock.root != access.root ?
				    lock.object : NULL,
				    lock.member, lock.offset, data_member,
				    insn->access->pos);
			}
			transfer_instruction(function, &states, insn);
		} END_FOR_EACH_PTR(insn);
		free_states(states);
	}
}

static bool
propagate_call_lock_conditions(struct function_info *function,
    struct state_entry *states, struct instruction *insn)
{
	struct function_info *callee = direct_callee(function, insn);
	struct lock_condition *condition;
	bool changed = false;

	if (callee == NULL)
		return (false);
	for (condition = callee->conditions; condition != NULL;
	    condition = condition->next) {
		struct locklint_access object;
		struct locklint_access lock;
		unsigned int caller_argument;

		if (!map_call_lock_condition(function, insn, condition,
		    &object, &lock))
			continue;
		if (get_state(states, &lock) != LOCK_NOT_HELD ||
		    !formal_argument(function->ep, object.root,
		    &caller_argument))
			continue;
		if (add_lock_condition(function, caller_argument,
		    condition->lock_root, condition->lock_object,
		    lock.member, lock.offset, condition->data_member,
		    condition->pos))
			changed = true;
	}
	return (changed);
}

static bool
propagate_function_lock_conditions(struct function_info *function)
{
	struct block_info *block;
	bool changed = false;

	for (block = function->blocks; block != NULL; block = block->next) {
		struct state_entry *states;
		struct instruction *insn;

		if (!block->reachable)
			continue;
		states = copy_states(block->in);
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb == NULL)
				continue;
			if (propagate_call_lock_conditions(function, states,
			    insn))
				changed = true;
			transfer_instruction(function, &states, insn);
		} END_FOR_EACH_PTR(insn);
		free_states(states);
	}
	return (changed);
}

static void
check_access(struct function_info *function, struct state_entry *states,
    struct instruction *insn)
{
	struct locklint_access access;
	struct locklint_access lock;
	struct symbol *data_member;
	enum lock_state state;
	unsigned int argument;

	if (!protected_access(function, insn, &access, &lock, &data_member))
		return;
	state = get_state(states, &lock);
	if (state == LOCK_NOT_HELD) {
		if (defer_formal_lock_conditions(function) &&
		    formal_argument(function->ep, access.root, &argument))
			return;
		warning(insn->access->pos,
		    "locklint: protected member '%s' accessed without "
		    "holding '%s'", show_ident(data_member->ident),
		    lock_name(&lock));
	} else if (state == LOCK_MAYBE_HELD) {
		warning(insn->access->pos,
		    "locklint: lock '%s' is not held on every path "
		    "accessing protected member '%s'", lock_name(&lock),
		    show_ident(data_member->ident));
	}
}

static void
check_direct_call(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	struct function_info *callee = direct_callee(function, insn);
	struct lock_condition *condition;
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
			enum lock_state state;
			unsigned int caller_argument;

			if (!map_call_lock_condition(function, insn, condition,
			    &object, &lock))
				continue;
			state = get_state(*states, &lock);
			if (state == LOCK_HELD)
				continue;
			if (state == LOCK_NOT_HELD &&
			    defer_formal_lock_conditions(function) &&
			    formal_argument(function->ep, object.root,
			    &caller_argument))
				continue;
			if (state == LOCK_NOT_HELD) {
				warning(pos, "locklint: call to '%s' accesses "
				    "protected member '%s' without holding '%s'",
				    show_ident(callee->ep->name->ident),
				    show_ident(condition->data_member->ident),
				    lock_name(&lock));
			} else {
				warning(pos, "locklint: lock '%s' is not held on "
				    "every path calling '%s'", lock_name(&lock),
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
			enum lock_state state;

			argument = call_argument(insn, transfer->argument);
			if (!argument_lock(function->tu, argument,
			    transfer->lock_member, transfer->lock_offset,
			    &lock))
				continue;
			state = get_state(*states, &lock);
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
			set_state(states, &lock, transfer->output[state]);
		}
	}
}

static void
check_lock_action(struct function_info *function,
    struct state_entry **states, struct instruction *insn)
{
	struct locklint_access lock;
	enum locklint_lock_action action;
	enum lock_state state;
	struct position pos;
	unsigned int argument;
	bool defer;

	action = locklint_get_lock_action(function->tu, insn, &lock);
	if (action == LOCKLINT_LOCK_NONE || lock.root == NULL)
		return;
	state = get_state(*states, &lock);
	defer = defer_formal_lock_conditions(function) &&
	    formal_argument(function->ep, lock.root, &argument);
	pos = insn->call_expr != NULL ? insn->call_expr->pos : insn->pos;
	if (action == LOCKLINT_LOCK_ACQUIRE) {
		if (state == LOCK_HELD && !defer) {
			warning(pos, "locklint: lock '%s' is already held",
			    lock_name(&lock));
		} else if (state == LOCK_MAYBE_HELD && !defer) {
			warning(pos, "locklint: lock '%s' may already be held",
			    lock_name(&lock));
		}
		set_state(states, &lock, LOCK_HELD);
	} else {
		if (state == LOCK_NOT_HELD && !defer) {
			warning(pos, "locklint: lock '%s' is not held",
			    lock_name(&lock));
		} else if (state == LOCK_MAYBE_HELD && !defer) {
			warning(pos, "locklint: lock '%s' may not be held",
			    lock_name(&lock));
		}
		set_state(states, &lock, LOCK_NOT_HELD);
	}
}

static void
check_return_state(struct function_info *function, struct block_info *block,
    struct state_entry *states)
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
	for (entry = states; entry != NULL; entry = entry->next) {
		if (!entry->side_effect)
			continue;
		if (entry->state == LOCK_HELD) {
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
		struct state_entry *states;
		struct instruction *insn;

		if (!block->reachable)
			continue;
		states = copy_states(block->in);
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb == NULL)
				continue;
			check_access(function, states, insn);
			check_direct_call(function, &states, insn);
			check_lock_action(function, &states, insn);
			transfer_assertion(function, &states, insn);
		} END_FOR_EACH_PTR(insn);
		check_return_state(function, block, states);
		free_states(states);
	}
}

static void
free_blocks(struct block_info *blocks)
{
	while (blocks != NULL) {
		struct block_info *next = blocks->next;

		free_states(blocks->in);
		free_states(blocks->out);
		free(blocks);
		blocks = next;
	}
}

void
locklint_check_add(struct translation_unit *tu, struct entrypoint *ep)
{
	struct function_info *function;

	if (!functions_by_entrypoint_initialized) {
		avl_create(&functions_by_entrypoint, compare_function_entrypoint,
		    sizeof (struct function_info),
		    offsetof(struct function_info, by_entrypoint));
		functions_by_entrypoint_initialized = true;
	}
	function = calloc(1, sizeof (*function));
	if (function == NULL)
		die("out of memory registering function analysis");
	function->tu = tu;
	function->ep = ep;
	avl_add(&functions_by_entrypoint, function);
	*functions_tail = function;
	functions_tail = &function->next;
}

void
locklint_check_all(void)
{
	struct function_info *function;
	bool changed;

	mark_direct_callers();
	for (function = functions; function != NULL; function = function->next)
		(void) collect_local_transfers(function);
	do {
		changed = false;
		for (function = functions; function != NULL;
		    function = function->next) {
			if (propagate_transfer_candidates(function))
				changed = true;
		}
		for (function = functions; function != NULL;
		    function = function->next) {
			if (solve_function_transfers(function))
				changed = true;
		}
	} while (changed);
	for (function = functions; function != NULL; function = function->next)
		function->blocks = analyze_blocks(function);
	for (function = functions; function != NULL; function = function->next)
		collect_local_lock_conditions(function);
	do {
		changed = false;
		for (function = functions; function != NULL;
		    function = function->next) {
			if (propagate_function_lock_conditions(function))
				changed = true;
		}
	} while (changed);
	for (function = functions; function != NULL; function = function->next)
		emit_diagnostics(function);

	while (functions != NULL) {
		struct function_info *next = functions->next;
		struct lock_condition *condition = functions->conditions;
		struct lock_transfer *transfer = functions->transfers;

		while (condition != NULL) {
			struct lock_condition *condition_next = condition->next;

			free(condition);
			condition = condition_next;
		}
		while (transfer != NULL) {
			struct lock_transfer *transfer_next = transfer->next;

			free(transfer);
			transfer = transfer_next;
		}
		avl_remove(&functions_by_entrypoint, functions);
		free_blocks(functions->blocks);
		free(functions);
		functions = next;
	}
	if (functions_by_entrypoint_initialized) {
		avl_destroy(&functions_by_entrypoint);
		functions_by_entrypoint_initialized = false;
	}
	functions_tail = &functions;
}
