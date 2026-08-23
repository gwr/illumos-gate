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

static struct state_entry *
transfer_block(struct basic_block *bb, struct state_entry *in)
{
	struct state_entry *out = copy_states(in);
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		struct locklint_access lock;
		enum locklint_lock_action action;

		if (insn->bb == NULL)
			continue;
		action = locklint_get_lock_action(insn, &lock);
		if (action == LOCKLINT_LOCK_ACQUIRE && lock.root != NULL)
			set_state(&out, &lock, LOCK_HELD);
		else if (action == LOCKLINT_LOCK_RELEASE && lock.root != NULL)
			set_state(&out, &lock, LOCK_NOT_HELD);
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

static const char *
lock_name(const struct locklint_access *lock)
{
	if (lock->member != NULL && lock->member->ident != NULL)
		return (show_ident(lock->member->ident));
	if (lock->root != NULL && lock->root->ident != NULL)
		return (show_ident(lock->root->ident));
	return ("<unknown>");
}

static void
check_access(struct state_entry *states, struct instruction *insn)
{
	struct locklint_access access;
	struct locklint_access lock;
	struct symbol *protector;
	enum lock_state state;

	if (insn->access == NULL ||
	    (insn->opcode != OP_LOAD && insn->opcode != OP_STORE) ||
	    !locklint_get_access(insn->access, &access) ||
	    access.member == NULL)
		return;
	protector = locklint_protecting_member(access.member);
	if (protector == NULL)
		return;
	lock.root = access.root;
	lock.member = protector;
	state = get_state(states, &lock);
	if (state == LOCK_NOT_HELD) {
		warning(insn->access->pos,
		    "locklint: protected member '%s' accessed without "
		    "holding '%s'", show_ident(access.member->ident),
		    lock_name(&lock));
	} else if (state == LOCK_MAYBE_HELD) {
		warning(insn->access->pos,
		    "locklint: lock '%s' is not held on every path "
		    "accessing protected member '%s'", lock_name(&lock),
		    show_ident(access.member->ident));
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
emit_diagnostics(struct entrypoint *ep, struct block_info *blocks)
{
	struct block_info *block;

	for (block = blocks; block != NULL; block = block->next) {
		struct state_entry *states;
		struct instruction *insn;

		if (!block->reachable)
			continue;
		states = copy_states(block->in);
		FOR_EACH_PTR(block->bb->insns, insn) {
			if (insn->bb == NULL)
				continue;
			check_access(states, insn);
			check_lock_action(&states, insn);
		} END_FOR_EACH_PTR(insn);
		check_return_state(ep, block, states);
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
locklint_check(struct entrypoint *ep)
{
	struct block_info *blocks;

	blocks = analyze_blocks(ep);
	emit_diagnostics(ep, blocks);
	free_blocks(blocks);
}
