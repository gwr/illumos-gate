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
 * The callgraph lifecycle is:
 *
 *     CONSTRUCTING -> RESOLVING -> READY -> CLEANED
 *
 * CONSTRUCTING collects linearized functions, function-address escapes,
 * function-pointer load/store evidence, and exact targets from supported
 * static const aggregate initializers.  Resolution is deliberately deferred
 * until every translation unit has been parsed, so function identity,
 * direct and indirect callees, ambiguous external definitions, roots, and
 * reachability are resolved with whole-program knowledge.
 *
 * callgraph_add() and callgraph_record_pointer_evidence() are valid only
 * while CONSTRUCTING.  callgraph_resolve() is valid only while CONSTRUCTING
 * and makes the graph READY.  Iterators, callee queries, and audit output
 * require READY.  Cleanup also requires READY, and all allocated iterators
 * must be closed before callgraph_cleanup() releases graph-owned records and
 * transitions to CLEANED.
 */

#include <errno.h>
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
#include "callgraph.h"
#include "function_info.h"
#include "identity.h"
#include "symbol.h"

enum callgraph_state {
	CALLGRAPH_CONSTRUCTING,
	CALLGRAPH_RESOLVING,
	CALLGRAPH_READY,
	CALLGRAPH_CLEANED
};

enum function_root_reason {
	FUNCTION_ROOT_EXTERNAL = 1 << 0,
	FUNCTION_ROOT_NO_DIRECT_CALLER = 1 << 1,
	FUNCTION_ROOT_POINTER_ESCAPE = 1 << 2
};

struct function_record {
	struct function_info info;
	bool has_nonself_caller;
	bool has_exact_escape;
	bool internal_linkage;
	bool inline_implementation;
	bool identity_lower_bound;
	unsigned int identity_sequence;
	avl_node_t by_entrypoint;
	avl_node_t by_identity;
	struct function_record *next;
};

struct function_escape {
	struct translation_unit *tu;
	struct symbol *symbol;
	struct function_info *target;
	struct position pos;
	bool closed_initializer;
	unsigned int sequence;
	avl_node_t by_source;
	avl_node_t by_target;
	struct function_escape *next;
};

struct indirect_target {
	struct translation_unit *tu;
	struct symbol *object;
	struct symbol *member;
	struct symbol *symbol;
	struct function_info *target;
	unsigned long offset;
	struct position pos;
	bool ambiguous;
	struct indirect_target *next;
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

struct callgraph_iter {
	struct function_record *next;
	struct callgraph_iter *open_next;
};

static enum callgraph_state callgraph_state = CALLGRAPH_CONSTRUCTING;
static struct callgraph_iter *open_iterators;
static struct function_record *functions;
static struct function_record **functions_tail = &functions;
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
static struct indirect_target *indirect_targets;
static struct indirect_target **indirect_targets_tail = &indirect_targets;
static struct function_pointer_activity *function_pointer_activities;
static struct function_pointer_activity **function_pointer_activities_tail =
    &function_pointer_activities;
static avl_tree_t function_pointer_activity_by_source;
static bool function_pointer_activity_index_initialized;
static bool record_function_pointer_activity;
static unsigned int next_function_pointer_activity_sequence;

static struct function_record *
function_record(struct function_info *function)
{
	return ((struct function_record *)((char *)function -
	    offsetof(struct function_record, info)));
}

static void
require_state(enum callgraph_state state, const char *operation)
{
	if (callgraph_state != state)
		die("callgraph %s in invalid state", operation);
}

static int
compare_function_entrypoint(const void *left_arg, const void *right_arg)
{
	const struct function_record *left = left_arg;
	const struct function_record *right = right_arg;

	return (AVL_PCMP(left->info.ep, right->info.ep));
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
	const struct function_record *left = left_arg;
	const struct function_record *right = right_arg;
	const struct symbol *left_definition = left->info.ep->name;
	const struct symbol *right_definition = right->info.ep->name;
	int result;

	result = AVL_CMP(left->internal_linkage, right->internal_linkage);
	if (result != 0)
		return (result);
	result = compare_ident(left_definition->ident, right_definition->ident);
	if (result != 0)
		return (result);
	if (left->internal_linkage) {
		result = AVL_PCMP(left->info.tu, right->info.tu);
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

static struct expression *
initializer_target_expression(struct expression *expr)
{
	for (;;) {
		if (expr == NULL)
			return (NULL);
		switch (expr->type) {
		case EXPR_POS:
			expr = expr->init_expr;
			break;
		case EXPR_CAST:
		case EXPR_FORCE_CAST:
		case EXPR_IMPLIED_CAST:
			expr = expr->cast_expression;
			break;
		case EXPR_PREOP:
			if (expr->op != '&')
				return (NULL);
			expr = expr->unop;
			break;
		default:
			return (expr);
		}
	}
}

static void
mark_closed_initializer_escape(struct translation_unit *tu,
    struct symbol *target, struct position pos)
{
	struct function_escape *escape;

	for (escape = function_escapes; escape != NULL; escape = escape->next) {
		if (escape->tu == tu && escape->symbol == target &&
		    compare_position(escape->pos, pos) == 0) {
			escape->closed_initializer = true;
			return;
		}
	}
}

static void
record_indirect_target(struct translation_unit *tu, struct symbol *object,
    struct symbol *member, unsigned long offset, struct symbol *target,
    struct position pos)
{
	struct indirect_target *entry;

	for (entry = indirect_targets; entry != NULL; entry = entry->next) {
		if (entry->tu != tu || entry->object != object ||
		    entry->member != member || entry->offset != offset)
			continue;
		if (entry->symbol != target)
			entry->ambiguous = true;
		return;
	}
	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory recording indirect-call target");
	entry->tu = tu;
	entry->object = object;
	entry->member = member;
	entry->symbol = target;
	entry->offset = offset;
	entry->pos = pos;
	*indirect_targets_tail = entry;
	indirect_targets_tail = &entry->next;
}

static void
record_static_initializer_targets(struct translation_unit *tu,
    struct symbol *object)
{
	struct expression *entry;
	struct expression *target_expr;
	struct expression *initializer = object->initializer;

	if ((object->ctype.modifiers &
	    (MOD_TOPLEVEL | MOD_STATIC | MOD_CONST)) !=
	    (MOD_TOPLEVEL | MOD_STATIC | MOD_CONST) ||
	    (object->ctype.modifiers & MOD_ADDRESSABLE) != 0 ||
	    initializer == NULL || initializer->type != EXPR_INITIALIZER)
		return;
	FOR_EACH_PTR(initializer->expr_list, entry) {
		if (entry->type != EXPR_POS || entry->init_nr != 1 ||
		    !function_pointer_type(entry->ctype))
			continue;
		target_expr = initializer_target_expression(entry);
		if (target_expr == NULL || target_expr->type != EXPR_SYMBOL ||
		    !function_symbol(target_expr->symbol))
			continue;
		record_indirect_target(tu, object, entry->ctype,
		    entry->init_offset, target_expr->symbol, target_expr->pos);
	} END_FOR_EACH_PTR(entry);
}

static void
record_static_indirect_targets(struct translation_unit *tu,
    struct symbol_list *symbols)
{
	struct symbol *symbol;

	FOR_EACH_PTR(symbols, symbol) {
		record_static_initializer_targets(tu, symbol);
	} END_FOR_EACH_PTR(symbol);
}

void
callgraph_record_pointer_evidence(struct translation_unit *tu,
    struct symbol_list *symbols, bool record_activity)
{
	static struct reporter reporter = {
		.r_symbol = report_function_pointer_symbol,
		.r_member = report_function_pointer_member,
	};

	require_state(CALLGRAPH_CONSTRUCTING, "evidence mutation");
	function_escape_tu = tu;
	record_function_pointer_activity = record_activity;
	dissect(symbols, &reporter);
	record_function_pointer_activity = false;
	record_static_indirect_targets(tu, symbols);
	function_escape_tu = NULL;
}

void
callgraph_add(struct translation_unit *tu, struct entrypoint *ep)
{
	struct function_record *function;

	require_state(CALLGRAPH_CONSTRUCTING, "mutation");
	if (!function_indexes_initialized) {
		avl_create(&functions_by_entrypoint, compare_function_entrypoint,
		    sizeof (struct function_record),
		    offsetof(struct function_record, by_entrypoint));
		avl_create(&functions_by_identity, compare_function_identity,
		    sizeof (struct function_record),
		    offsetof(struct function_record, by_identity));
		function_indexes_initialized = true;
	}
	function = calloc(1, sizeof (*function));
	if (function == NULL)
		die("out of memory registering function analysis");
	function->info.tu = tu;
	function->info.ep = ep;
	function->internal_linkage =
	    (ep->name->ctype.modifiers & MOD_STATIC) != 0;
	function->inline_implementation =
	    ep->name->gnu_inline;
	if (++next_function_identity_sequence == 0)
		die("too many functions for identity index");
	function->identity_sequence = next_function_identity_sequence;
	avl_add(&functions_by_entrypoint, function);
	avl_add(&functions_by_identity, function);
	*functions_tail = function;
	functions_tail = &function->next;
}

static struct function_record *
find_function(struct entrypoint *ep)
{
	struct function_record key = { 0 };

	if (!function_indexes_initialized)
		return (NULL);
	key.info.ep = ep;
	return (avl_find(&functions_by_entrypoint, &key, NULL));
}

static bool
same_ident(struct ident *left, struct ident *right)
{
	return (compare_ident(left, right) == 0);
}

static struct function_record *
find_identity_function(struct translation_unit *tu, struct symbol *symbol,
    bool internal_linkage)
{
	struct entrypoint entrypoint = { 0 };
	struct function_record key = { 0 };
	struct function_record *function;
	avl_index_t where;

	if (!function_indexes_initialized || symbol->ident == NULL)
		return (NULL);
	entrypoint.name = symbol;
	key.info.tu = tu;
	key.info.ep = &entrypoint;
	key.internal_linkage = internal_linkage;
	key.identity_lower_bound = true;
	(void) avl_find(&functions_by_identity, &key, &where);
	function = avl_nearest(&functions_by_identity, where, AVL_AFTER);
	if (function == NULL ||
	    function->internal_linkage != internal_linkage ||
	    (internal_linkage && function->info.tu != tu) ||
	    !same_ident(function->info.ep->name->ident, symbol->ident))
		return (NULL);
	return (function);
}

static struct function_record *
find_internal_function(struct translation_unit *tu, struct symbol *symbol)
{
	return (find_identity_function(tu, symbol, true));
}

static struct function_record *
find_external_function(struct symbol *symbol, bool *ambiguous)
{
	struct function_record *function;
	struct function_record *match = NULL;

	*ambiguous = false;
	if (symbol->ident == NULL ||
	    (symbol->ctype.modifiers & MOD_STATIC) != 0)
		return (NULL);
	/*
	 * External records sort before internal records and then by identifier,
	 * so all emitted definitions for this name form one contiguous range.
	 */
	function = find_identity_function(NULL, symbol, false);
	for (; function != NULL && !function->internal_linkage &&
	    same_ident(symbol->ident, function->info.ep->name->ident);
	    function = AVL_NEXT(&functions_by_identity, function)) {
		if (function->inline_implementation)
			continue;
		if (match != NULL) {
			*ambiguous = true;
			return (NULL);
		}
		match = function;
	}
	return (match);
}

static struct function_info *
resolve_function_symbol(struct translation_unit *tu, struct symbol *symbol,
    bool use_inline_implementation, bool *ambiguous)
{
	struct function_record *function;

	*ambiguous = false;
	function = find_internal_function(tu, symbol);
	/*
	 * Prefer the translation unit's static definition only when it is
	 * visible to this declaration.  An intervening local can make a nested
	 * extern refer to an external function with the same name.
	 */
	if (function != NULL && locklint_symbol_can_use_internal(symbol))
		return (&function->info);
	if (symbol->ep == NULL && symbol->definition != NULL &&
	    (use_inline_implementation ||
	    !symbol->definition->gnu_inline))
		symbol = symbol->definition;
	if (symbol->ep != NULL) {
		function = find_function(symbol->ep);
		if (function != NULL && (use_inline_implementation ||
		    !function->inline_implementation) &&
		    (!function->inline_implementation ||
		    function->info.tu == tu))
			return (&function->info);
	}
	function = find_external_function(symbol, ambiguous);
	return (function != NULL ? &function->info : NULL);
}

static void
resolve_function_escapes(void)
{
	struct function_escape *escape;

	for (escape = function_escapes; escape != NULL;
	    escape = escape->next) {
		struct function_record *target;
		bool ambiguous;

		escape->target = resolve_function_symbol(escape->tu,
		    escape->symbol, false, &ambiguous);
		if (escape->target == NULL)
			continue;
		target = function_record(escape->target);
		target->has_exact_escape = true;
		if (!escape->closed_initializer) {
			escape->target->root_reasons |=
			    FUNCTION_ROOT_POINTER_ESCAPE;
		}
		avl_add(&function_escapes_by_target, escape);
	}
}

static void
resolve_indirect_targets(void)
{
	struct indirect_target *entry;

	for (entry = indirect_targets; entry != NULL; entry = entry->next) {
		bool ambiguous;

		if (entry->ambiguous)
			continue;
		entry->target = resolve_function_symbol(entry->tu, entry->symbol,
		    false, &ambiguous);
		if (ambiguous) {
			entry->ambiguous = true;
			continue;
		}
		if (entry->target != NULL) {
			mark_closed_initializer_escape(entry->tu, entry->symbol,
			    entry->pos);
		}
	}
}

static struct function_info *
direct_callee(const struct function_info *caller,
    const struct instruction *insn)
{
	bool ambiguous;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL)
		return (NULL);
	return (resolve_function_symbol(caller->tu, insn->func->sym,
	    true, &ambiguous));
}

static struct function_info *
indirect_callee(const struct function_info *caller,
    const struct instruction *insn)
{
	struct locklint_access access;
	struct indirect_target *entry;

	if (insn->opcode != OP_CALL || insn->call_expr == NULL ||
	    !locklint_get_access(caller->tu, insn->call_expr->fn, &access))
		return (NULL);
	for (entry = indirect_targets; entry != NULL; entry = entry->next) {
		if (!entry->ambiguous && entry->target != NULL &&
		    entry->tu == caller->tu && entry->object == access.root &&
		    entry->member == access.member &&
		    entry->offset == access.offset)
			return (entry->target);
	}
	return (NULL);
}

static struct function_info *
call_callee_impl(const struct function_info *caller,
    const struct instruction *insn)
{
	struct function_info *callee;

	callee = direct_callee(caller, insn);
	return (callee != NULL ? callee : indirect_callee(caller, insn));
}

static bool
ambiguous_callee_impl(const struct function_info *caller,
    const struct instruction *insn)
{
	struct function_info *function;
	bool ambiguous;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL)
		return (false);
	function = resolve_function_symbol(caller->tu, insn->func->sym,
	    true, &ambiguous);
	return (function == NULL && ambiguous);
}

struct function_info *
callgraph_callee(const struct function_info *caller,
    const struct instruction *insn)
{
	require_state(CALLGRAPH_READY, "callee query");
	return (call_callee_impl(caller, insn));
}

bool
callgraph_ambiguous_callee(const struct function_info *caller,
    const struct instruction *insn)
{
	require_state(CALLGRAPH_READY, "ambiguous callee query");
	return (ambiguous_callee_impl(caller, insn));
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
			callee = call_callee_impl(function, insn);
			if (callee != NULL)
				mark_reachable(callee);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

/*
 * First account for every non-self resolved incoming edge, then assign all
 * applicable root reasons and propagate reachability from the resulting roots.
 */
static void
classify_roots(void)
{
	struct function_record *function;

	for (function = functions; function != NULL; function = function->next) {
		struct basic_block *bb;

		FOR_EACH_PTR(function->info.ep->bbs, bb) {
			struct instruction *insn;

			FOR_EACH_PTR(bb->insns, insn) {
				struct function_info *callee;

				if (insn->bb == NULL)
					continue;
				callee = call_callee_impl(&function->info, insn);
				if (callee != NULL && callee != &function->info)
					function_record(callee)->
					    has_nonself_caller = true;
			} END_FOR_EACH_PTR(insn);
		} END_FOR_EACH_PTR(bb);
	}
	for (function = functions; function != NULL; function = function->next) {
		unsigned long modifiers =
		    function->info.ep->name->ctype.modifiers;

		if (!function->internal_linkage &&
		    !function->inline_implementation)
			function->info.root_reasons |= FUNCTION_ROOT_EXTERNAL;
		if (!function->has_nonself_caller &&
		    !function->inline_implementation)
			function->info.root_reasons |=
			    FUNCTION_ROOT_NO_DIRECT_CALLER;
		/*
		 * Sparse marks ordinary external definitions addressable.
		 * They are already roots, so use this fallback only for an
		 * internal function without an exact recorded escape.
		 */
		if (function->internal_linkage &&
		    (modifiers & MOD_ADDRESSABLE) != 0 &&
		    !function->has_exact_escape)
			function->info.root_reasons |=
			    FUNCTION_ROOT_POINTER_ESCAPE;
	}
	for (function = functions; function != NULL; function = function->next) {
		if (function->info.root_reasons != 0)
			mark_reachable(&function->info);
	}
}

void
callgraph_resolve(void)
{
	require_state(CALLGRAPH_CONSTRUCTING, "resolution");
	callgraph_state = CALLGRAPH_RESOLVING;
	resolve_indirect_targets();
	resolve_function_escapes();
	classify_roots();
	callgraph_state = CALLGRAPH_READY;
}

int
callgraph_iter_open(struct callgraph_iter **iterp)
{
	struct callgraph_iter *iter;

	if (callgraph_state != CALLGRAPH_READY)
		return (EBUSY);
	iter = calloc(1, sizeof (*iter));
	if (iter == NULL)
		return (ENOMEM);
	iter->next = functions;
	iter->open_next = open_iterators;
	open_iterators = iter;
	*iterp = iter;
	return (0);
}

struct function_info *
callgraph_iter_next(struct callgraph_iter *iter)
{
	struct function_record *function;

	require_state(CALLGRAPH_READY, "iteration");
	if (iter == NULL || iter->next == NULL)
		return (NULL);
	function = iter->next;
	iter->next = function->next;
	return (&function->info);
}

void
callgraph_iter_close(struct callgraph_iter *iter)
{
	struct callgraph_iter **link;

	require_state(CALLGRAPH_READY, "iterator close");
	for (link = &open_iterators; *link != NULL;
	    link = &(*link)->open_next) {
		if (*link != iter)
			continue;
		*link = iter->open_next;
		free(iter);
		return;
	}
	die("closing invalid callgraph iterator");
}

static struct position
call_position(const struct instruction *insn)
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
dump_function_calls(FILE *stream, struct function_info *function)
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

		(void) fprintf(stream, "  call %s:%u:%u ",
		    stream_name(calls[index].pos.stream),
		    calls[index].pos.line, calls[index].pos.pos);
		if (callee != NULL) {
			(void) fprintf(stream, "resolved %s tu=%s\n",
			    function_name(callee),
			    locklint_translation_unit_file(callee->tu));
			continue;
		}
		callee = indirect_callee(function, insn);
		if (callee != NULL) {
			(void) fprintf(stream, "resolved-indirect %s tu=%s\n",
			    function_name(callee),
			    locklint_translation_unit_file(callee->tu));
			continue;
		}
		if (ambiguous_callee_impl(function, insn)) {
			(void) fprintf(stream, "ambiguous-external\n");
			continue;
		}
		if (insn->func != NULL && insn->func->type == PSEUDO_SYM)
			symbol = insn->func->sym;
		if (function_symbol(symbol)) {
			(void) fprintf(stream, "unresolved-external %s\n",
			    symbol->ident != NULL ?
			    show_ident(symbol->ident) : "<anonymous>");
		} else {
			(void) fprintf(stream, "indirect\n");
		}
	}
	free(calls);
}

void
callgraph_dump(FILE *stream)
{
	struct function_escape *escape;
	struct function_pointer_activity *activity;
	struct function_record *function;

	require_state(CALLGRAPH_READY, "dump");
	for (function = functions; function != NULL; function = function->next) {
		struct symbol *definition = function->info.ep->name;

		(void) fprintf(stream, "function %s tu=%s definition=%s:%u:%u "
		    "linkage=%s reachable=%s\n", function_name(&function->info),
		    locklint_translation_unit_file(function->info.tu),
		    stream_name(definition->pos.stream), definition->pos.line,
		    definition->pos.pos,
		    function->internal_linkage ? "internal" : "external",
		    function->info.reachable_from_root ? "yes" : "no");
		if ((function->info.root_reasons &
		    FUNCTION_ROOT_EXTERNAL) != 0)
			(void) fprintf(stream, "  root external-linkage\n");
		if ((function->info.root_reasons &
		    FUNCTION_ROOT_NO_DIRECT_CALLER) != 0)
			(void) fprintf(stream, "  root no-known-direct-caller\n");
		if ((function->info.root_reasons &
		    FUNCTION_ROOT_POINTER_ESCAPE) != 0) {
			(void) fprintf(stream, "  root function-pointer-escape");
			if (!function->has_exact_escape) {
				(void) fprintf(stream, " fallback=%s:%u:%u",
				    stream_name(definition->pos.stream),
				    definition->pos.line, definition->pos.pos);
			}
			(void) fprintf(stream, "\n");
		}
		dump_function_calls(stream, &function->info);
	}
	(void) fprintf(stream, "escapes\n");
	for (escape = function_escape_indexes_initialized ?
	    avl_first(&function_escapes_by_source) : NULL;
	    escape != NULL;
	    escape = AVL_NEXT(&function_escapes_by_source, escape)) {
		(void) fprintf(stream, "  escape %s:%u:%u %s ",
		    stream_name(escape->pos.stream), escape->pos.line,
		    escape->pos.pos,
		    escape->symbol->ident != NULL ?
		    show_ident(escape->symbol->ident) : "<anonymous>");
		if (escape->target != NULL) {
			(void) fprintf(stream, "resolved %s tu=%s\n",
			    function_name(escape->target),
			    locklint_translation_unit_file(escape->target->tu));
		} else {
			(void) fprintf(stream, "unresolved\n");
		}
	}
	(void) fprintf(stream, "pointer activity\n");
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
		(void) fprintf(stream, "  %s %s:%u:%u ", kind,
		    stream_name(activity->pos.stream), activity->pos.line,
		    activity->pos.pos);
		if (activity->member != NULL) {
			(void) fprintf(stream, "%s.%s\n",
			    activity->symbol->ident != NULL ?
			    show_ident(activity->symbol->ident) : "<anonymous>",
			    activity->member->ident != NULL ?
			    show_ident(activity->member->ident) : "<anonymous>");
		} else {
			(void) fprintf(stream, "%s\n",
			    activity->symbol->ident != NULL ?
			    show_ident(activity->symbol->ident) : "<anonymous>");
		}
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

static void
free_indirect_targets(void)
{
	while (indirect_targets != NULL) {
		struct indirect_target *next = indirect_targets->next;

		free(indirect_targets);
		indirect_targets = next;
	}
	indirect_targets_tail = &indirect_targets;
}

void
callgraph_cleanup(void)
{
	struct function_record *function;

	require_state(CALLGRAPH_READY, "cleanup");
	if (open_iterators != NULL)
		die("cleaning callgraph with active iterators");
	for (function = functions; function != NULL; function = function->next) {
		if (function->info.blocks != NULL ||
		    function->info.conditions != NULL ||
		    function->info.assumptions != NULL ||
		    function->info.transfers != NULL ||
		    function->info.visibility_transfers != NULL) {
			die("cleaning callgraph with checker attachments");
		}
	}
	free_indirect_targets();
	free_function_pointer_activity();
	free_function_escapes();
	while (functions != NULL) {
		struct function_record *next = functions->next;

		avl_remove(&functions_by_entrypoint, functions);
		avl_remove(&functions_by_identity, functions);
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
	callgraph_state = CALLGRAPH_CLEANED;
}
