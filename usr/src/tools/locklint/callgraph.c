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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "avl.h"
#include "dissect.h"
#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "assertions.h"
#include "callgraph.h"
#include "context.h"
#include "diagnostics.h"
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
	FUNCTION_ROOT_POINTER_ESCAPE = 1 << 2,
	FUNCTION_ROOT_DECLARED_ENTRY = 1 << 3
};

enum function_external_entry {
	FUNCTION_EXTERNAL_ENTRY_UNSPECIFIED,
	FUNCTION_EXTERNAL_ENTRY_TRUE,
	FUNCTION_EXTERNAL_ENTRY_FALSE
};

struct call_target_set;

struct function_record {
	struct function_info info;
	const struct call_target_set *singleton_targets;
	bool has_nonself_caller;
	bool has_exact_escape;
	bool internal_linkage;
	bool inline_implementation;
	bool identity_lower_bound;
	unsigned int identity_sequence;
	const char *entry_declaration_file;
	unsigned long entry_declaration_line;
	enum function_external_entry external_entry;
	const char *external_entry_file;
	unsigned long external_entry_line;
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

struct unanalyzed_callback {
	struct translation_unit *tu;
	struct symbol *symbol;
	struct position pos;
	bool internal_linkage;
	avl_node_t by_identity;
	struct unanalyzed_callback *next;
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

struct call_target_set {
	size_t count;
	avl_node_t by_value;
	struct function_info *targets[];
};

enum call_target_kind {
	CALL_TARGET_DIRECT,
	CALL_TARGET_INDIRECT
};

enum call_target_state {
	CALL_TARGET_RESOLVED,
	CALL_TARGET_UNRESOLVED,
	CALL_TARGET_AMBIGUOUS
};

struct call_target_entry {
	struct instruction *instruction;
	const struct call_target_set *targets;
	enum call_target_kind kind;
	enum call_target_state state;
	bool direct_ambiguous;
	avl_node_t node;
};

struct callgraph_block_info {
	avl_tree_t entries;
	struct call_target_entry entry[];
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
static struct unanalyzed_callback *unanalyzed_callbacks;
static struct unanalyzed_callback **unanalyzed_callbacks_tail =
    &unanalyzed_callbacks;
static avl_tree_t unanalyzed_callbacks_by_identity;
static bool unanalyzed_callback_index_initialized;
static struct indirect_target *indirect_targets;
static struct indirect_target **indirect_targets_tail = &indirect_targets;
static struct function_pointer_activity *function_pointer_activities;
static struct function_pointer_activity **function_pointer_activities_tail =
    &function_pointer_activities;
static avl_tree_t function_pointer_activity_by_source;
static bool function_pointer_activity_index_initialized;
static bool record_function_pointer_activity;
static unsigned int next_function_pointer_activity_sequence;
static avl_tree_t call_target_sets;
static bool call_target_sets_initialized;
static bool has_declared_entries;

static int
compare_call_target_entry(const void *left_arg, const void *right_arg)
{
	const struct call_target_entry *left = left_arg;
	const struct call_target_entry *right = right_arg;

	return (AVL_CMP(left->instruction, right->instruction));
}

static struct function_record *
function_record(struct function_info *function)
{
	return ((struct function_record *)((char *)function -
	    offsetof(struct function_record, info)));
}

static int
compare_call_target_set(const void *left_arg, const void *right_arg)
{
	const struct call_target_set *left = left_arg;
	const struct call_target_set *right = right_arg;
	size_t count = left->count < right->count ? left->count : right->count;
	size_t index;
	int result;

	for (index = 0; index < count; index++) {
		result = AVL_CMP(function_record(left->targets[index])->
		    identity_sequence, function_record(right->targets[index])->
		    identity_sequence);
		if (result != 0)
			return (result);
	}
	return (AVL_CMP(left->count, right->count));
}

static int
compare_call_target_qsort(const void *left_arg, const void *right_arg)
{
	struct function_info *const *left = left_arg;
	struct function_info *const *right = right_arg;

	return (AVL_CMP(function_record(*left)->identity_sequence,
	    function_record(*right)->identity_sequence));
}

/*
 * Intern immutable target arrays so repeated calls to the same function or
 * target combination share storage.  Target order follows stable function
 * registration order and duplicates are removed before lookup.
 */
static const struct call_target_set *
call_target_set_intern(struct function_info *const *targets, size_t count)
{
	struct call_target_set *candidate;
	struct call_target_set *set;
	avl_index_t where;
	size_t index;
	size_t unique;
	size_t size;

	if (count == 0 || targets == NULL ||
	    count > (SIZE_MAX - sizeof (*candidate)) /
	    sizeof (*candidate->targets))
		die("invalid call target set size");
	size = sizeof (*candidate) + count * sizeof (*candidate->targets);
	candidate = calloc(1, size);
	if (candidate == NULL)
		die("out of memory interning call target set");
	candidate->count = count;
	(void) memcpy(candidate->targets, targets,
	    count * sizeof (*candidate->targets));
	qsort(candidate->targets, count, sizeof (*candidate->targets),
	    compare_call_target_qsort);
	for (index = 0, unique = 0; index < count; index++) {
		if (candidate->targets[index] == NULL) {
			free(candidate);
			die("null function in call target set");
		}
		if (unique != 0 &&
		    candidate->targets[unique - 1] == candidate->targets[index])
			continue;
		candidate->targets[unique++] = candidate->targets[index];
	}
	candidate->count = unique;
	if (!call_target_sets_initialized) {
		avl_create(&call_target_sets, compare_call_target_set,
		    sizeof (*candidate), offsetof(struct call_target_set,
		    by_value));
		call_target_sets_initialized = true;
	}
	set = avl_find(&call_target_sets, candidate, &where);
	if (set != NULL) {
		free(candidate);
		return (set);
	}
	avl_insert(&call_target_sets, candidate, where);
	return (candidate);
}

static void
build_singleton_target_sets(void)
{
	struct function_record *function;

	for (function = functions; function != NULL; function = function->next) {
		struct function_info *target = &function->info;

		function->singleton_targets =
		    call_target_set_intern(&target, 1);
	}
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
compare_unanalyzed_callback(const void *left_arg, const void *right_arg)
{
	const struct unanalyzed_callback *left = left_arg;
	const struct unanalyzed_callback *right = right_arg;
	int result;

	result = AVL_CMP(left->internal_linkage, right->internal_linkage);
	if (result != 0)
		return (result);
	result = compare_ident(left->symbol->ident, right->symbol->ident);
	if (result != 0)
		return (result);
	if (left->internal_linkage)
		return (AVL_PCMP(left->tu, right->tu));
	return (0);
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
	function->info.assumed_regions_tail =
	    &function->info.assumed_regions;
	context_collection_create(&function->info);
	binding_collection_create(&function->info.bindings);
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

/*
 * Sparse may omit inherited static linkage from a definition that follows a
 * static prototype.  Recover that same-translation-unit definition without
 * allowing a genuinely external declaration to bind to it.
 */
static struct function_record *
find_inherited_internal_definition(struct translation_unit *tu,
    struct symbol *symbol, bool use_inline_implementation, bool *ambiguous)
{
	struct function_record *function;
	struct function_record *match = NULL;

	*ambiguous = false;
	if (!locklint_symbol_can_use_internal(symbol))
		return (NULL);
	function = find_identity_function(NULL, symbol, false);
	for (; function != NULL && !function->internal_linkage &&
	    same_ident(symbol->ident, function->info.ep->name->ident);
	    function = AVL_NEXT(&functions_by_identity, function)) {
		if (function->info.tu != tu ||
		    (!use_inline_implementation &&
		    function->inline_implementation))
			continue;
		if (match != NULL) {
			*ambiguous = true;
			return (NULL);
		}
		match = function;
	}
	return (match);
}

/*
 * Attach a command-file entry contract to the unique external definition.
 * The first successful declaration also selects explicit external-entry
 * scope for root classification.
 */
enum callgraph_declare_result
callgraph_declare_entry_no_competing_threads(const char *name,
    const char *file, unsigned long line)
{
	struct symbol symbol = {
		.ident = built_in_ident(name)
	};
	struct function_record *function;
	bool ambiguous;

	require_state(CALLGRAPH_CONSTRUCTING, "entry declaration");
	function = find_external_function(&symbol, &ambiguous);
	if (ambiguous)
		return (CALLGRAPH_DECLARE_AMBIGUOUS);
	if (function == NULL)
		return (CALLGRAPH_DECLARE_UNRESOLVED);
	function->info.entry_no_competing_threads = true;
	function->info.root_reasons |= FUNCTION_ROOT_DECLARED_ENTRY;
	if (function->entry_declaration_file == NULL) {
		function->entry_declaration_file = file;
		function->entry_declaration_line = line;
	}
	has_declared_entries = true;
	return (CALLGRAPH_DECLARE_OK);
}

/*
 * Record an explicit external-entry property on the unique external
 * definition.  Repeated equal declarations are harmless, while contradictory
 * declarations are rejected independently of command-file order.
 */
enum callgraph_declare_result
callgraph_declare_external_entry(const char *name, bool value,
    const char *file, unsigned long line)
{
	struct symbol symbol = {
		.ident = built_in_ident(name)
	};
	struct function_record *function;
	enum function_external_entry setting = value ?
	    FUNCTION_EXTERNAL_ENTRY_TRUE : FUNCTION_EXTERNAL_ENTRY_FALSE;
	bool ambiguous;

	require_state(CALLGRAPH_CONSTRUCTING, "external-entry declaration");
	function = find_external_function(&symbol, &ambiguous);
	if (ambiguous)
		return (CALLGRAPH_DECLARE_AMBIGUOUS);
	if (function == NULL)
		return (CALLGRAPH_DECLARE_UNRESOLVED);
	if (function->external_entry != FUNCTION_EXTERNAL_ENTRY_UNSPECIFIED &&
	    function->external_entry != setting)
		return (CALLGRAPH_DECLARE_CONFLICT);
	if (function->external_entry == FUNCTION_EXTERNAL_ENTRY_UNSPECIFIED) {
		function->external_entry = setting;
		function->external_entry_file = file;
		function->external_entry_line = line;
	}
	return (CALLGRAPH_DECLARE_OK);
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
	if (function == NULL) {
		function = find_inherited_internal_definition(tu, symbol,
		    use_inline_implementation, ambiguous);
		if (function != NULL || *ambiguous)
			return (function != NULL ? &function->info : NULL);
	}
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

/*
 * Build one record per missing callback target by walking escapes in source
 * order.  The first insertion therefore supplies stable diagnostic evidence,
 * while the identity AVL makes repeated operation-table entries inexpensive.
 */
static void
collect_unanalyzed_callbacks(void)
{
	struct function_escape *escape;

	if (!function_escape_indexes_initialized)
		return;
	avl_create(&unanalyzed_callbacks_by_identity,
	    compare_unanalyzed_callback, sizeof (struct unanalyzed_callback),
	    offsetof(struct unanalyzed_callback, by_identity));
	unanalyzed_callback_index_initialized = true;
	for (escape = avl_first(&function_escapes_by_source);
	    escape != NULL;
	    escape = AVL_NEXT(&function_escapes_by_source, escape)) {
		struct unanalyzed_callback key = { 0 };
		struct unanalyzed_callback *callback;

		if (escape->target != NULL || escape->symbol->ident == NULL)
			continue;
		key.tu = escape->tu;
		key.symbol = escape->symbol;
		key.internal_linkage =
		    (escape->symbol->ctype.modifiers & MOD_STATIC) != 0;
		if (avl_find(&unanalyzed_callbacks_by_identity, &key,
		    NULL) != NULL)
			continue;
		callback = calloc(1, sizeof (*callback));
		if (callback == NULL)
			die("out of memory recording unanalyzed callback");
		callback->tu = key.tu;
		callback->symbol = key.symbol;
		callback->pos = escape->pos;
		callback->internal_linkage = key.internal_linkage;
		avl_add(&unanalyzed_callbacks_by_identity, callback);
		*unanalyzed_callbacks_tail = callback;
		unanalyzed_callbacks_tail = &callback->next;
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

static void
resolve_indirect_callee(const struct function_info *caller,
    const struct instruction *insn, struct function_info **calleep,
    bool *ambiguousp)
{
	struct locklint_access access;
	struct indirect_target *entry;

	*calleep = NULL;
	*ambiguousp = false;
	if (insn->opcode != OP_CALL || insn->call_expr == NULL ||
	    !locklint_get_access(caller->tu, insn->call_expr->fn, &access))
		return;
	for (entry = indirect_targets; entry != NULL; entry = entry->next) {
		if (entry->tu == caller->tu && entry->object == access.root &&
		    entry->member == access.member &&
		    entry->offset == access.offset) {
			*calleep = entry->target;
			*ambiguousp = entry->ambiguous;
			return;
		}
	}
}

/*
 * Resolve every live call once after whole-program target evidence is final.
 * A null basic_block.priv then authoritatively means that the block contains
 * no live calls; a populated tree contains every live call in the block.
 */
static void
build_call_target_cache(void)
{
	struct function_record *function;

	for (function = functions; function != NULL; function = function->next) {
		struct basic_block *bb;

		FOR_EACH_PTR(function->info.ep->bbs, bb) {
			struct callgraph_block_info *info;
			struct instruction *insn;
			size_t count = 0;
			size_t index = 0;

			FOR_EACH_PTR(bb->insns, insn) {
				if (insn->bb != NULL && insn->opcode == OP_CALL)
					count++;
			} END_FOR_EACH_PTR(insn);
			bb->priv = NULL;
			if (count == 0)
				continue;
			if (count > (SIZE_MAX - sizeof (*info)) /
			    sizeof (*info->entry))
				die("too many call instructions in one block");
			info = calloc(1, sizeof (*info) +
			    count * sizeof (*info->entry));
			if (info == NULL)
				die("out of memory caching call targets");
			avl_create(&info->entries, compare_call_target_entry,
			    sizeof (*info->entry),
			    offsetof(struct call_target_entry, node));
			FOR_EACH_PTR(bb->insns, insn) {
				struct call_target_entry *entry;
				struct function_info *indirect_callee = NULL;
				bool direct;
				bool direct_ambiguous = false;
				bool indirect_ambiguous = false;

				if (insn->bb == NULL || insn->opcode != OP_CALL)
					continue;
				entry = &info->entry[index++];
				entry->instruction = insn;
				direct = insn->func != NULL &&
				    insn->func->type == PSEUDO_SYM &&
				    insn->func->sym != NULL;
				if (direct) {
					entry->kind = CALL_TARGET_DIRECT;
					indirect_callee =
					    resolve_function_symbol(
					    function->info.tu, insn->func->sym,
					    true, &direct_ambiguous);
				}
				if (indirect_callee == NULL) {
					resolve_indirect_callee(&function->info,
					    insn, &indirect_callee,
					    &indirect_ambiguous);
					if (indirect_callee != NULL) {
						entry->kind = CALL_TARGET_INDIRECT;
					}
				}
				if (!direct)
					entry->kind = CALL_TARGET_INDIRECT;
				entry->direct_ambiguous = direct_ambiguous;
				if (indirect_callee != NULL) {
					entry->targets = function_record(
					    indirect_callee)->singleton_targets;
					entry->state = CALL_TARGET_RESOLVED;
				} else if (direct_ambiguous ||
				    indirect_ambiguous) {
					entry->state = CALL_TARGET_AMBIGUOUS;
				} else {
					entry->state = CALL_TARGET_UNRESOLVED;
				}
				avl_add(&info->entries, entry);
			} END_FOR_EACH_PTR(insn);
			bb->priv = info;
		} END_FOR_EACH_PTR(bb);
	}
}

static struct call_target_entry *
find_call_target(const struct instruction *insn)
{
	struct callgraph_block_info *info;
	struct call_target_entry key = { 0 };
	struct call_target_entry *entry;

	if (insn == NULL || insn->bb == NULL || insn->opcode != OP_CALL)
		return (NULL);
	info = insn->bb->priv;
	if (info == NULL)
		die("call instruction has no block target cache");
	key.instruction = (struct instruction *)insn;
	entry = avl_find(&info->entries, &key, NULL);
	if (entry == NULL)
		die("call instruction is absent from its block target cache");
	return (entry);
}

static struct function_info *
call_callee_impl(const struct instruction *insn)
{
	struct call_target_entry *entry = find_call_target(insn);

	if (entry == NULL || entry->targets == NULL ||
	    entry->targets->count != 1)
		return (NULL);
	return (entry->targets->targets[0]);
}

static size_t
call_target_count_impl(const struct instruction *insn)
{
	struct call_target_entry *entry = find_call_target(insn);

	return (entry != NULL && entry->targets != NULL ?
	    entry->targets->count : 0);
}

static struct function_info *
call_target_impl(const struct instruction *insn, size_t index)
{
	struct call_target_entry *entry = find_call_target(insn);

	if (entry == NULL || entry->targets == NULL ||
	    index >= entry->targets->count)
		return (NULL);
	return (entry->targets->targets[index]);
}

static bool
ambiguous_callee_impl(const struct instruction *insn)
{
	struct call_target_entry *entry = find_call_target(insn);

	return (entry != NULL && entry->direct_ambiguous);
}

struct function_info *
callgraph_callee(const struct function_info *caller,
    const struct instruction *insn)
{
	require_state(CALLGRAPH_READY, "callee query");
	(void) caller;
	return (call_callee_impl(insn));
}

size_t
callgraph_target_count(const struct function_info *caller,
    const struct instruction *insn)
{
	require_state(CALLGRAPH_READY, "target count query");
	(void) caller;
	return (call_target_count_impl(insn));
}

struct function_info *
callgraph_target(const struct function_info *caller,
    const struct instruction *insn, size_t index)
{
	require_state(CALLGRAPH_READY, "target query");
	(void) caller;
	return (call_target_impl(insn, index));
}

const struct call_target_set *
callgraph_targets(const struct function_info *caller,
    const struct instruction *insn)
{
	struct call_target_entry *entry;

	require_state(CALLGRAPH_READY, "target-set query");
	(void) caller;
	entry = find_call_target(insn);
	return (entry != NULL ? entry->targets : NULL);
}

const struct call_target_set *
callgraph_symbol_targets(const struct function_info *caller,
    const struct symbol *symbol)
{
	struct function_info *target;
	bool ambiguous;

	require_state(CALLGRAPH_READY, "symbol target query");
	if (caller == NULL || symbol == NULL || !function_symbol(symbol))
		return (NULL);
	target = resolve_function_symbol(caller->tu, (struct symbol *)symbol,
	    true, &ambiguous);
	if (target == NULL || ambiguous)
		return (NULL);
	return (function_record(target)->singleton_targets);
}

size_t
callgraph_target_set_count(const struct call_target_set *targets)
{
	require_state(CALLGRAPH_READY, "target set count query");
	return (targets != NULL ? targets->count : 0);
}

struct function_info *
callgraph_target_set_target(const struct call_target_set *targets,
    size_t index)
{
	require_state(CALLGRAPH_READY, "target set query");
	if (targets == NULL || index >= targets->count)
		return (NULL);
	return (targets->targets[index]);
}

bool
callgraph_ambiguous_callee(const struct function_info *caller,
    const struct instruction *insn)
{
	require_state(CALLGRAPH_READY, "ambiguous callee query");
	(void) caller;
	return (ambiguous_callee_impl(insn));
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
			size_t count;
			size_t index;

			if (insn->bb == NULL)
				continue;
			count = call_target_count_impl(insn);
			for (index = 0; index < count; index++)
				mark_reachable(call_target_impl(insn,
				    index));
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
				size_t count;
				size_t index;

				if (insn->bb == NULL)
					continue;
				count = call_target_count_impl(insn);
				for (index = 0; index < count; index++) {
					struct function_info *callee =
					    call_target_impl(insn, index);

					if (callee != &function->info)
						function_record(callee)->
						    has_nonself_caller = true;
				}
			} END_FOR_EACH_PTR(insn);
		} END_FOR_EACH_PTR(bb);
	}
	for (function = functions; function != NULL; function = function->next) {
		unsigned long modifiers =
		    function->info.ep->name->ctype.modifiers;
		bool external_entry;

		external_entry =
		    function->external_entry == FUNCTION_EXTERNAL_ENTRY_TRUE ||
		    (function->external_entry ==
		    FUNCTION_EXTERNAL_ENTRY_UNSPECIFIED &&
		    !has_declared_entries);
		if (external_entry && !function->internal_linkage &&
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
	build_singleton_target_sets();
	resolve_indirect_targets();
	resolve_function_escapes();
	collect_unanalyzed_callbacks();
	build_call_target_cache();
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
			if (insn->bb != NULL && insn->opcode == OP_CALL &&
			    !locklint_is_assertion_consumer(insn))
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
			if (insn->bb == NULL || insn->opcode != OP_CALL ||
			    locklint_is_assertion_consumer(insn))
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
		struct call_target_entry *entry = find_call_target(insn);
		struct function_info *callee = call_callee_impl(insn);
		struct symbol *symbol = NULL;

		(void) fprintf(stream, "  call %s:%u:%u ",
		    stream_name(calls[index].pos.stream),
		    calls[index].pos.line, calls[index].pos.pos);
		if (callee != NULL) {
			(void) fprintf(stream, "%s %s tu=%s\n",
			    entry->kind == CALL_TARGET_DIRECT ?
			    "resolved" : "resolved-indirect",
			    function_name(callee),
			    locklint_translation_unit_file(callee->tu));
			continue;
		}
		if (entry->direct_ambiguous) {
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
		if ((function->info.root_reasons &
		    FUNCTION_ROOT_DECLARED_ENTRY) != 0) {
			(void) fprintf(stream,
			    "  root declared-entry no-competing-threads %s:%lu\n",
			    function->entry_declaration_file,
			    function->entry_declaration_line);
		}
		if (function->external_entry !=
		    FUNCTION_EXTERNAL_ENTRY_UNSPECIFIED) {
			(void) fprintf(stream,
			    "  property external-entry=%s %s:%lu\n",
			    function->external_entry ==
			    FUNCTION_EXTERNAL_ENTRY_TRUE ? "true" : "false",
			    function->external_entry_file,
			    function->external_entry_line);
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

void
callgraph_report_unanalyzed_callbacks(void)
{
	struct unanalyzed_callback *callback;

	require_state(CALLGRAPH_READY, "unanalyzed callback reporting");
	for (callback = unanalyzed_callbacks; callback != NULL;
	    callback = callback->next) {
		locklint_warning(LOCKLINT_DIAG_UNANALYZED_CALLBACK,
		    callback->pos,
		    "function '%s' escapes as a callback, but no unique "
		    "definition is available",
		    show_ident(callback->symbol->ident));
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

static void
free_call_target_sets(void)
{
	struct call_target_set *set;
	void *cookie = NULL;

	if (!call_target_sets_initialized)
		return;
	while ((set = avl_destroy_nodes(&call_target_sets, &cookie)) != NULL)
		free(set);
	avl_destroy(&call_target_sets);
	call_target_sets_initialized = false;
}

static void
free_unanalyzed_callbacks(void)
{
	while (unanalyzed_callbacks != NULL) {
		struct unanalyzed_callback *next =
		    unanalyzed_callbacks->next;

		avl_remove(&unanalyzed_callbacks_by_identity,
		    unanalyzed_callbacks);
		free(unanalyzed_callbacks);
		unanalyzed_callbacks = next;
	}
	if (unanalyzed_callback_index_initialized) {
		avl_destroy(&unanalyzed_callbacks_by_identity);
		unanalyzed_callback_index_initialized = false;
	}
	unanalyzed_callbacks_tail = &unanalyzed_callbacks;
}

void
callgraph_cleanup(void)
{
	require_state(CALLGRAPH_READY, "cleanup");
	if (open_iterators != NULL)
		die("cleaning callgraph with active iterators");
	free_call_target_sets();
	free_indirect_targets();
	free_unanalyzed_callbacks();
	free_function_pointer_activity();
	free_function_escapes();
	while (functions != NULL) {
		struct function_record *next = functions->next;
		struct assumed_region *region;

		avl_remove(&functions_by_entrypoint, functions);
		avl_remove(&functions_by_identity, functions);
		context_collection_free(&functions->info);
		binding_collection_free(&functions->info.bindings);
		free(functions->info.derived_protectors);
		free(functions->info.stored_target_demands);
		if (functions->info.operation_family_profile_count != 0) {
			struct operation_family_profile *profile;

			while ((profile = avl_first(
			    &functions->info.operation_family_profiles)) !=
			    NULL) {
				struct operation_family_entry *entry;

				avl_remove(
				    &functions->info.operation_family_profiles,
				    profile);
				while ((entry =
				    avl_first(&profile->entries)) != NULL) {
					avl_remove(&profile->entries, entry);
					free(entry);
				}
				avl_destroy(&profile->entries);
				free(profile);
			}
			avl_destroy(&functions->info.operation_family_profiles);
		}
		while ((region = functions->info.assumed_regions) != NULL) {
			functions->info.assumed_regions = region->next;
			free(region->name);
			free(region->mutex_name);
			free(region);
		}
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
	has_declared_entries = false;
	callgraph_state = CALLGRAPH_CLEANED;
}
