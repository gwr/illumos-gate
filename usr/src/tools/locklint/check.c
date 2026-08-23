#include <stdbool.h>
#include <stdlib.h>

#include "lib.h"
#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "annotations.h"
#include "check.h"
#include "events.h"
#include "symbol.h"

enum lock_state {
	LOCK_NOT_HELD,
	LOCK_HELD,
	LOCK_MAYBE_HELD
};

struct state_entry {
	struct locklint_access lock;
	enum lock_state state;
	struct state_entry *next;
};

struct block_info {
	struct basic_block *bb;
	bool reachable;
	struct state_entry *in;
	struct state_entry *out;
	struct block_info *next;
};

struct requirement {
	unsigned int argument;
	struct symbol *lock_member;
	struct symbol *data_member;
	struct position pos;
	struct requirement *next;
};

struct function_info {
	struct entrypoint *ep;
	struct block_info *blocks;
	struct requirement *requirements;
	bool has_direct_caller;
	bool reachable_from_root;
	struct function_info *next;
};

static struct function_info *functions;
static struct function_info **functions_tail = &functions;

static bool
same_lock(const struct locklint_access *left,
    const struct locklint_access *right)
{
	return (left->root == right->root && left->member == right->member);
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

static struct state_entry *
copy_states(struct state_entry *states)
{
	struct state_entry *copy = NULL;
	struct state_entry **tail = &copy;
	struct state_entry *entry;

	for (entry = states; entry != NULL; entry = entry->next) {
		*tail = alloc_state(&entry->lock, entry->state);
		tail = &(*tail)->next;
	}
	return (copy);
}

static bool
same_states(struct state_entry *left, struct state_entry *right)
{
	struct state_entry *entry;

	for (entry = left; entry != NULL; entry = entry->next) {
		if (get_state(right, &entry->lock) != entry->state)
			return (false);
	}
	for (entry = right; entry != NULL; entry = entry->next) {
		if (get_state(left, &entry->lock) != entry->state)
			return (false);
	}
	return (true);
}

static void
merge_states(struct state_entry **merged, struct state_entry *incoming)
{
	struct state_entry *entry;

	for (entry = *merged; entry != NULL; entry = entry->next) {
		if (get_state(incoming, &entry->lock) != entry->state)
			entry->state = LOCK_MAYBE_HELD;
	}
	for (entry = incoming; entry != NULL; entry = entry->next) {
		if (get_state(*merged, &entry->lock) == LOCK_NOT_HELD)
			set_state(merged, &entry->lock, LOCK_MAYBE_HELD);
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
transfer_lock_action(struct state_entry **states, struct instruction *insn)
{
	struct locklint_access lock;
	enum locklint_lock_action action;

	action = locklint_get_lock_action(insn, &lock);
	if (action == LOCKLINT_LOCK_ACQUIRE && lock.root != NULL)
		set_state(states, &lock, LOCK_HELD);
	else if (action == LOCKLINT_LOCK_RELEASE && lock.root != NULL)
		set_state(states, &lock, LOCK_NOT_HELD);
}

static struct state_entry *
transfer_block(struct basic_block *bb, struct state_entry *in)
{
	struct state_entry *out = copy_states(in);
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (insn->bb == NULL)
			continue;
		transfer_lock_action(&out, insn);
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
analyze_blocks(struct entrypoint *ep)
{
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
			out = transfer_block(block->bb, in);
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

static struct function_info *
find_function(struct entrypoint *ep)
{
	struct function_info *function;

	for (function = functions; function != NULL; function = function->next) {
		if (function->ep == ep)
			return (function);
	}
	return (NULL);
}

static struct function_info *
direct_callee(struct instruction *insn)
{
	struct symbol *symbol;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL)
		return (NULL);
	symbol = insn->func->sym;
	if (symbol->ep == NULL && symbol->definition != NULL)
		symbol = symbol->definition;
	return (symbol->ep != NULL ? find_function(symbol->ep) : NULL);
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

static bool
add_requirement(struct function_info *function, unsigned int argument,
    struct symbol *lock_member, struct symbol *data_member,
    struct position pos)
{
	struct requirement *requirement;

	for (requirement = function->requirements; requirement != NULL;
	    requirement = requirement->next) {
		if (requirement->argument == argument &&
		    requirement->lock_member == lock_member &&
		    requirement->data_member == data_member)
			return (false);
	}
	requirement = calloc(1, sizeof (*requirement));
	if (requirement == NULL)
		die("out of memory recording lock requirement");
	requirement->argument = argument;
	requirement->lock_member = lock_member;
	requirement->data_member = data_member;
	requirement->pos = pos;
	requirement->next = function->requirements;
	function->requirements = requirement;
	return (true);
}

static bool
defer_formal_requirements(struct function_info *function)
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
protected_access(struct instruction *insn, struct locklint_access *access,
    struct locklint_access *lock, struct symbol **data_member)
{
	struct symbol *protector;

	if (insn->access == NULL ||
	    (insn->opcode != OP_LOAD && insn->opcode != OP_STORE) ||
	    !locklint_get_access(insn->access, access) ||
	    access->member == NULL)
		return (false);
	protector = locklint_protecting_member(access->member);
	if (protector == NULL)
		return (false);
	lock->root = access->root;
	lock->member = protector;
	*data_member = access->member;
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
			callee = direct_callee(insn);
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
				callee = direct_callee(insn);
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

static void
collect_local_requirements(struct function_info *function)
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
			if (protected_access(insn, &access, &lock, &data_member) &&
			    get_state(states, &lock) == LOCK_NOT_HELD &&
			    formal_argument(function->ep, access.root,
			    &argument)) {
				(void) add_requirement(function, argument,
				    lock.member, data_member, insn->access->pos);
			}
			transfer_lock_action(&states, insn);
		} END_FOR_EACH_PTR(insn);
		free_states(states);
	}
}

static bool
propagate_call_requirements(struct function_info *function,
    struct state_entry *states, struct instruction *insn)
{
	struct function_info *callee = direct_callee(insn);
	struct requirement *requirement;
	bool changed = false;

	if (callee == NULL)
		return (false);
	for (requirement = callee->requirements; requirement != NULL;
	    requirement = requirement->next) {
		struct locklint_access access;
		struct locklint_access lock;
		struct expression *argument;
		unsigned int caller_argument;

		argument = call_argument(insn, requirement->argument);
		if (!locklint_get_access(argument, &access))
			continue;
		lock.root = access.root;
		lock.member = requirement->lock_member;
		if (get_state(states, &lock) != LOCK_NOT_HELD ||
		    !formal_argument(function->ep, access.root,
		    &caller_argument))
			continue;
		if (add_requirement(function, caller_argument,
		    requirement->lock_member, requirement->data_member,
		    requirement->pos))
			changed = true;
	}
	return (changed);
}

static bool
propagate_function_requirements(struct function_info *function)
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
			if (propagate_call_requirements(function, states, insn))
				changed = true;
			transfer_lock_action(&states, insn);
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

	if (!protected_access(insn, &access, &lock, &data_member))
		return;
	state = get_state(states, &lock);
	if (state == LOCK_NOT_HELD) {
		if (defer_formal_requirements(function) &&
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
    struct state_entry *states, struct instruction *insn)
{
	struct function_info *callee = direct_callee(insn);
	struct requirement *requirement;
	struct position pos;

	if (callee == NULL || callee == function)
		return;
	pos = insn->call_expr != NULL ? insn->call_expr->pos : insn->pos;
	for (requirement = callee->requirements; requirement != NULL;
	    requirement = requirement->next) {
		struct locklint_access access;
		struct locklint_access lock;
		struct expression *argument;
		enum lock_state state;
		unsigned int caller_argument;

		argument = call_argument(insn, requirement->argument);
		if (!locklint_get_access(argument, &access))
			continue;
		lock.root = access.root;
		lock.member = requirement->lock_member;
		state = get_state(states, &lock);
		if (state == LOCK_HELD)
			continue;
		if (state == LOCK_NOT_HELD &&
		    defer_formal_requirements(function) &&
		    formal_argument(function->ep, access.root,
		    &caller_argument))
			continue;
		if (state == LOCK_NOT_HELD) {
			warning(pos, "locklint: call to '%s' accesses protected "
			    "member '%s' without holding '%s'",
			    show_ident(callee->ep->name->ident),
			    show_ident(requirement->data_member->ident),
			    lock_name(&lock));
		} else {
			warning(pos, "locklint: lock '%s' is not held on every "
			    "path calling '%s'", lock_name(&lock),
			    show_ident(callee->ep->name->ident));
		}
	}
}

static void
check_lock_action(struct state_entry **states, struct instruction *insn)
{
	struct locklint_access lock;
	enum locklint_lock_action action;
	enum lock_state state;
	struct position pos;

	action = locklint_get_lock_action(insn, &lock);
	if (action == LOCKLINT_LOCK_NONE || lock.root == NULL)
		return;
	state = get_state(*states, &lock);
	pos = insn->call_expr != NULL ? insn->call_expr->pos : insn->pos;
	if (action == LOCKLINT_LOCK_ACQUIRE) {
		if (state == LOCK_HELD) {
			warning(pos, "locklint: lock '%s' is already held",
			    lock_name(&lock));
		} else if (state == LOCK_MAYBE_HELD) {
			warning(pos, "locklint: lock '%s' may already be held",
			    lock_name(&lock));
		}
		set_state(states, &lock, LOCK_HELD);
	} else {
		if (state == LOCK_NOT_HELD) {
			warning(pos, "locklint: lock '%s' is not held",
			    lock_name(&lock));
		} else if (state == LOCK_MAYBE_HELD) {
			warning(pos, "locklint: lock '%s' may not be held",
			    lock_name(&lock));
		}
		set_state(states, &lock, LOCK_NOT_HELD);
	}
}

static void
check_return_state(struct entrypoint *ep, struct block_info *block,
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
		if (entry->state == LOCK_HELD) {
			warning(pos, "locklint: lock '%s' held on return from '%s'",
			    lock_name(&entry->lock), show_ident(ep->name->ident));
		} else {
			warning(pos, "locklint: lock '%s' held on only some paths "
			    "returning from '%s'", lock_name(&entry->lock),
			    show_ident(ep->name->ident));
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
			check_direct_call(function, states, insn);
			check_lock_action(&states, insn);
		} END_FOR_EACH_PTR(insn);
		check_return_state(function->ep, block, states);
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
locklint_check_add(struct entrypoint *ep)
{
	struct function_info *function;

	function = calloc(1, sizeof (*function));
	if (function == NULL)
		die("out of memory registering function analysis");
	function->ep = ep;
	function->blocks = analyze_blocks(ep);
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
		collect_local_requirements(function);
	do {
		changed = false;
		for (function = functions; function != NULL;
		    function = function->next) {
			if (propagate_function_requirements(function))
				changed = true;
		}
	} while (changed);
	for (function = functions; function != NULL; function = function->next)
		emit_diagnostics(function);

	while (functions != NULL) {
		struct function_info *next = functions->next;
		struct requirement *requirement = functions->requirements;

		while (requirement != NULL) {
			struct requirement *requirement_next = requirement->next;

			free(requirement);
			requirement = requirement_next;
		}
		free_blocks(functions->blocks);
		free(functions);
		functions = next;
	}
	functions_tail = &functions;
}
