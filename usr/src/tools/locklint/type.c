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
 * Retain named aggregate types after Sparse removes their translation-unit
 * namespace bindings.  Locklint types coalesce exact Sparse types with the
 * same source origin, while indexes preserve exact-instance and name lookup.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"
#include "lib.h"
#include "statistics.h"
#include "symbol.h"
#include "type.h"

struct type_name_to_sparse_type {
	struct ident *name;
	struct symbol *type;
	avl_node_t by_name;
};

/*
 * Use ll_type rather than the overly generic "type" because this module
 * also works extensively with exact Sparse types.  Related names retain
 * type as their normal module prefix.
 */
struct ll_type {
	struct symbol *representative;
	unsigned long modifiers;
	struct ident *address_space;
	size_t instance_count;
	struct ll_type *base;
	struct ll_type **arguments;
	size_t argument_count;
	struct type_member *members;
	size_t member_count;
	avl_node_t by_origin;
	avl_node_t by_shape;
};

struct sparse_type_to_ll_type {
	struct symbol *exact;
	struct ll_type *type;
	avl_node_t by_exact;
};

struct sparse_member_to_type_member {
	struct symbol *exact;
	struct type_member *member;
	avl_node_t by_exact;
};

struct type_name_to_ll_type {
	struct ident *name;
	struct ll_type *type;
	avl_node_t by_name;
};

static avl_tree_t type_name_to_sparse_type_index;
static avl_tree_t types_by_origin;
static avl_tree_t types_by_shape;
static avl_tree_t sparse_type_to_ll_type_index;
static avl_tree_t sparse_member_to_type_member_index;
static avl_tree_t type_name_to_ll_type_index;

static enum type
type_kind(const struct ll_type *type)
{
	enum type kind = type->representative->type;

	return (kind == SYM_TYPEDEF ? SYM_NODE : kind);
}

static int
type_name_to_sparse_type_compare(const void *left_arg, const void *right_arg)
{
	const struct type_name_to_sparse_type *left = left_arg;
	const struct type_name_to_sparse_type *right = right_arg;
	int result;

	statistics.type_registry_comparisons++;
	result = AVL_PCMP(left->name, right->name);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->type, right->type));
}

static int
type_origin_compare(const void *left_arg, const void *right_arg)
{
	const struct ll_type *left = left_arg;
	const struct ll_type *right = right_arg;
	struct position left_pos = left->representative->pos;
	struct position right_pos = right->representative->pos;
	int result;

	result = AVL_CMP(type_kind(left), type_kind(right));
	if (result != 0)
		return (result);
	result = strcmp(stream_name(left_pos.stream),
	    stream_name(right_pos.stream));
	if (result != 0)
		return (AVL_ISIGN(result));
	result = AVL_CMP(left_pos.line, right_pos.line);
	if (result != 0)
		return (result);
	return (AVL_CMP(left_pos.pos, right_pos.pos));
}

static int
type_shape_compare(const void *left_arg, const void *right_arg)
{
	const struct ll_type *left = left_arg;
	const struct ll_type *right = right_arg;
	size_t index;
	int result;

	result = AVL_CMP(type_kind(left), type_kind(right));
	if (result != 0)
		return (result);
	result = AVL_CMP(left->modifiers, right->modifiers);
	if (result != 0)
		return (result);
	result = AVL_CMP(left->representative->ctype.alignment,
	    right->representative->ctype.alignment);
	if (result != 0)
		return (result);
	result = AVL_CMP(left->representative->bit_size,
	    right->representative->bit_size);
	if (result != 0)
		return (result);
	result = AVL_PCMP(left->address_space, right->address_space);
	if (result != 0)
		return (result);

	switch (type_kind(left)) {
	case SYM_BASETYPE:
		return (AVL_PCMP(
		    left->representative->ctype.base_type != NULL ?
		    left->representative->ctype.base_type :
		    left->representative,
		    right->representative->ctype.base_type != NULL ?
		    right->representative->ctype.base_type :
		    right->representative));
	case SYM_NODE:
	case SYM_PTR:
	case SYM_ARRAY:
		return (AVL_PCMP(left->base, right->base));
	case SYM_FN:
		result = AVL_CMP(left->representative->variadic,
		    right->representative->variadic);
		if (result != 0)
			return (result);
		result = AVL_PCMP(left->base, right->base);
		if (result != 0)
			return (result);
		result = AVL_CMP(left->argument_count,
		    right->argument_count);
		if (result != 0)
			return (result);
		for (index = 0; index < left->argument_count; index++) {
			result = AVL_PCMP(left->arguments[index],
			    right->arguments[index]);
			if (result != 0)
				return (result);
		}
		return (0);
	default:
		die("unsupported type shape kind %d", type_kind(left));
	}
}

static int
sparse_type_to_ll_type_compare(const void *left_arg, const void *right_arg)
{
	const struct sparse_type_to_ll_type *left = left_arg;
	const struct sparse_type_to_ll_type *right = right_arg;

	return (AVL_PCMP(left->exact, right->exact));
}

static int
sparse_member_to_type_member_compare(const void *left_arg,
    const void *right_arg)
{
	const struct sparse_member_to_type_member *left = left_arg;
	const struct sparse_member_to_type_member *right = right_arg;

	return (AVL_PCMP(left->exact, right->exact));
}

static int
type_name_to_ll_type_compare(const void *left_arg, const void *right_arg)
{
	const struct type_name_to_ll_type *left = left_arg;
	const struct type_name_to_ll_type *right = right_arg;
	int result;

	result = AVL_PCMP(left->name, right->name);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->type, right->type));
}

void
type_registry_create(void)
{
	avl_create(&type_name_to_sparse_type_index,
	    type_name_to_sparse_type_compare,
	    sizeof (struct type_name_to_sparse_type),
	    offsetof(struct type_name_to_sparse_type, by_name));
	avl_create(&types_by_origin, type_origin_compare,
	    sizeof (struct ll_type), offsetof(struct ll_type, by_origin));
	avl_create(&types_by_shape, type_shape_compare,
	    sizeof (struct ll_type), offsetof(struct ll_type, by_shape));
	avl_create(&sparse_type_to_ll_type_index,
	    sparse_type_to_ll_type_compare,
	    sizeof (struct sparse_type_to_ll_type),
	    offsetof(struct sparse_type_to_ll_type, by_exact));
	avl_create(&sparse_member_to_type_member_index,
	    sparse_member_to_type_member_compare,
	    sizeof (struct sparse_member_to_type_member),
	    offsetof(struct sparse_member_to_type_member, by_exact));
	avl_create(&type_name_to_ll_type_index, type_name_to_ll_type_compare,
	    sizeof (struct type_name_to_ll_type),
	    offsetof(struct type_name_to_ll_type, by_name));
}

void
type_registry_destroy(void)
{
	struct type_name_to_sparse_type *entry;
	struct type_name_to_ll_type *name;
	struct sparse_type_to_ll_type *exact;
	struct sparse_member_to_type_member *member;
	struct ll_type *type;
	void *cookie = NULL;

	while ((entry = avl_destroy_nodes(&type_name_to_sparse_type_index,
	    &cookie)) != NULL)
		free(entry);
	avl_destroy(&type_name_to_sparse_type_index);
	cookie = NULL;
	while ((name = avl_destroy_nodes(&type_name_to_ll_type_index,
	    &cookie)) != NULL)
		free(name);
	avl_destroy(&type_name_to_ll_type_index);
	cookie = NULL;
	while ((exact = avl_destroy_nodes(&sparse_type_to_ll_type_index,
	    &cookie)) != NULL)
		free(exact);
	avl_destroy(&sparse_type_to_ll_type_index);
	cookie = NULL;
	while ((member = avl_destroy_nodes(
	    &sparse_member_to_type_member_index, &cookie)) != NULL)
		free(member);
	avl_destroy(&sparse_member_to_type_member_index);
	cookie = NULL;
	while ((type = avl_destroy_nodes(&types_by_shape, &cookie)) != NULL) {
		free(type->arguments);
		free(type);
	}
	avl_destroy(&types_by_shape);
	cookie = NULL;
	while ((type = avl_destroy_nodes(&types_by_origin, &cookie)) != NULL) {
		free(type->members);
		free(type);
	}
	avl_destroy(&types_by_origin);
}

struct symbol *
type_node_strip(struct symbol *type)
{
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type);
}

struct symbol *
type_compound_resolve(struct symbol *type)
{
	type = type_node_strip(type);
	while (type != NULL && type->type == SYM_ARRAY)
		type = type_node_strip(type->ctype.base_type);
	if (type == NULL ||
	    (type->type != SYM_STRUCT && type->type != SYM_UNION))
		return (NULL);
	examine_symbol_type(type);
	return (type);
}

static bool
type_has_origin(enum type kind)
{
	return (kind == SYM_STRUCT || kind == SYM_UNION || kind == SYM_ENUM);
}

static struct ll_type *
type_origin_intern(struct symbol *exact, bool *created)
{
	struct ll_type key = {
		.representative = exact
	};
	struct ll_type *type;
	avl_index_t where;

	type = avl_find(&types_by_origin, &key, &where);
	if (type != NULL) {
		*created = false;
		return (type);
	}
	type = calloc(1, sizeof (*type));
	if (type == NULL)
		die("out of memory recording type");
	type->representative = exact;
	type->modifiers = exact->ctype.modifiers & ~MOD_IGNORE;
	type->address_space = exact->ctype.as;
	avl_insert(&types_by_origin, type, where);
	*created = true;
	return (type);
}

static struct ll_type *
type_exact_find(struct symbol *symbol, avl_index_t *where)
{
	struct sparse_type_to_ll_type key = {
		.exact = symbol
	};
	struct sparse_type_to_ll_type *exact;

	exact = avl_find(&sparse_type_to_ll_type_index, &key, where);
	return (exact == NULL ? NULL : exact->type);
}

static void
type_exact_record(struct symbol *symbol, struct ll_type *type)
{
	struct sparse_type_to_ll_type key = {
		.exact = symbol
	};
	struct sparse_type_to_ll_type *exact;
	avl_index_t where;

	exact = avl_find(&sparse_type_to_ll_type_index, &key, &where);
	if (exact != NULL)
		return;
	exact = calloc(1, sizeof (*exact));
	if (exact == NULL)
		die("out of memory recording exact type");
	exact->exact = symbol;
	exact->type = type;
	avl_insert(&sparse_type_to_ll_type_index, exact, where);
	type->instance_count++;
}

static void
type_member_record(struct symbol *symbol, struct type_member *member)
{
	struct sparse_member_to_type_member key = {
		.exact = symbol
	};
	struct sparse_member_to_type_member *exact;
	avl_index_t where;

	exact = avl_find(&sparse_member_to_type_member_index, &key, &where);
	if (exact != NULL)
		return;
	exact = calloc(1, sizeof (*exact));
	if (exact == NULL)
		die("out of memory recording exact member");
	exact->exact = symbol;
	exact->member = member;
	avl_insert(&sparse_member_to_type_member_index, exact, where);
}

static unsigned long
type_semantic_modifiers(const struct symbol *exact)
{
	return (exact->ctype.modifiers & ~MOD_IGNORE);
}

/*
 * Intern a completed non-aggregate description.  Recursive references have
 * already been converted to ll_type pointers, so the comparator need not walk
 * the type graph.
 */
static struct ll_type *
type_shape_intern(struct ll_type *key)
{
	struct ll_type *type;
	avl_index_t where;

	type = avl_find(&types_by_shape, key, &where);
	if (type != NULL) {
		free(key->arguments);
		return (type);
	}
	type = calloc(1, sizeof (*type));
	if (type == NULL)
		die("out of memory recording type shape");
	*type = *key;
	avl_insert(&types_by_shape, type, where);
	return (type);
}

static struct ll_type *type_intern(struct symbol *);

/*
 * Build the declaration-order member array for a new aggregate type.  The
 * aggregate's exact-type mapping is installed first by type_intern(), allowing
 * a member pointer to refer back to the aggregate under construction.
 */
static void
type_members_build(struct ll_type *type, struct symbol *exact)
{
	struct symbol *symbol;
	size_t index = 0;

	if (exact->type != SYM_STRUCT && exact->type != SYM_UNION)
		return;
	type->member_count = ptr_list_size(
	    (struct ptr_list *)exact->symbol_list);
	if (type->member_count != 0) {
		type->members = calloc(type->member_count,
		    sizeof (*type->members));
		if (type->members == NULL)
			die("out of memory recording type members");
	}
	FOR_EACH_PTR(exact->symbol_list, symbol) {
		struct type_member *member = &type->members[index++];

		member->representative = symbol;
		member->type = type_intern(symbol->ctype.base_type);
		if (member->type == NULL)
			die("unsupported Sparse member type");
		type_member_record(symbol, member);
	} END_FOR_EACH_PTR(symbol);
}

/*
 * Normalize Sparse's transparent node chain into one qualified type.  Each
 * inner node is interned first so every exact Sparse node still receives a
 * translation entry.
 */
static struct ll_type *
type_node_intern(struct symbol *exact)
{
	struct ll_type key = {
		.representative = exact,
		.modifiers = type_semantic_modifiers(exact) &
		    (MOD_QUALIFIER | MOD_SAFE | MOD_BITWISE | MOD_NOCAST |
		    MOD_NODEREF | MOD_NORETURN),
		.address_space = exact->ctype.as
	};
	struct ll_type *base_type;

	base_type = type_intern(exact->ctype.base_type);
	if (base_type == NULL)
		return (NULL);
	if (type_kind(base_type) == SYM_NODE) {
		key.modifiers |= base_type->modifiers;
		if (key.address_space == NULL)
			key.address_space = base_type->address_space;
		key.base = base_type->base;
	} else {
		key.base = base_type;
	}
	if (key.modifiers == 0 && key.address_space == NULL &&
	    exact->ctype.alignment == base_type->representative->ctype.alignment &&
	    (exact->bit_size == 0 ||
	    exact->bit_size == base_type->representative->bit_size))
		return (base_type);
	return (type_shape_intern(&key));
}

/*
 * Build a function key from its canonical result and argument types.  The
 * argument array becomes owned by a new type or is discarded on an AVL hit.
 */
static struct ll_type *
type_function_intern(struct symbol *exact)
{
	struct ll_type key = {
		.representative = exact,
		.modifiers = type_semantic_modifiers(exact),
		.address_space = exact->ctype.as
	};
	struct symbol *argument;
	size_t index = 0;

	key.base = type_intern(exact->ctype.base_type);
	if (key.base == NULL)
		return (NULL);
	key.argument_count = ptr_list_size(
	    (struct ptr_list *)exact->arguments);
	if (key.argument_count != 0) {
		key.arguments = calloc(key.argument_count,
		    sizeof (*key.arguments));
		if (key.arguments == NULL)
			die("out of memory recording function type");
	}
	FOR_EACH_PTR(exact->arguments, argument) {
		key.arguments[index++] = type_intern(argument);
		if (key.arguments[index - 1] == NULL) {
			free(key.arguments);
			return (NULL);
		}
	} END_FOR_EACH_PTR(argument);
	return (type_shape_intern(&key));
}

/*
 * Translate one exact Sparse type into the locklint graph.  Origin-bearing
 * declarations use source identity; all other types use canonical shape.
 */
static struct ll_type *
type_intern(struct symbol *exact)
{
	struct ll_type key = {
		.representative = exact
	};
	struct ll_type *type;
	bool created;

	if (exact == NULL)
		return (NULL);
	type = type_exact_find(exact, NULL);
	if (type != NULL)
		return (type);
	examine_symbol_type(exact);

	if (type_has_origin(exact->type)) {
		type = type_origin_intern(exact, &created);
		type_exact_record(exact, type);
		if (created)
			type_members_build(type, exact);
		return (type);
	} else {
		key.modifiers = type_semantic_modifiers(exact);
		key.address_space = exact->ctype.as;
		switch (exact->type) {
		case SYM_BASETYPE:
			type = type_shape_intern(&key);
			break;
		case SYM_NODE:
		case SYM_TYPEDEF:
			type = type_node_intern(exact);
			break;
		case SYM_PTR:
		case SYM_ARRAY:
			key.base = type_intern(exact->ctype.base_type);
			if (key.base == NULL)
				return (NULL);
			type = type_shape_intern(&key);
			break;
		case SYM_FN:
			type = type_function_intern(exact);
			break;
		case SYM_BITFIELD:
			type = type_intern(exact->ctype.base_type);
			break;
		default:
			return (NULL);
		}
	}
	if (type != NULL)
		type_exact_record(exact, type);
	return (type);
}

static void
type_name_record(struct ident *name, struct ll_type *type)
{
	struct type_name_to_ll_type key = {
		.name = name,
		.type = type
	};
	struct type_name_to_ll_type *entry;
	avl_index_t where;

	entry = avl_find(&type_name_to_ll_type_index, &key, &where);
	if (entry != NULL)
		return;
	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory recording type name");
	entry->name = name;
	entry->type = type;
	avl_insert(&type_name_to_ll_type_index, entry, where);
}

static void
type_registry_record(struct ident *name, struct symbol *type)
{
	struct type_name_to_sparse_type key;
	struct type_name_to_sparse_type *entry;
	struct ll_type *ll_type;
	avl_index_t where;

	if (name == NULL)
		return;
	type = type_compound_resolve(type);
	if (type == NULL)
		return;
	ll_type = type_intern(type);
	type_name_record(name, ll_type);
	key.name = name;
	key.type = type;
	statistics.type_registry_find++;
	entry = avl_find(&type_name_to_sparse_type_index, &key, &where);
	if (entry != NULL) {
		statistics.type_registry_duplicates++;
		return;
	}
	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory recording named type");
	entry->name = name;
	entry->type = type;
	avl_insert(&type_name_to_sparse_type_index, entry, where);
	statistics.type_registry_insertions++;
}

static void
type_tree_register(struct symbol *type)
{
	struct symbol *argument;
	struct symbol *unwrapped;

	statistics.type_registration_nodes_visited++;
	if (type == NULL)
		return;
	(void) type_intern(type);
	unwrapped = type_node_strip(type);
	if (unwrapped == NULL)
		return;
	switch (unwrapped->type) {
	case SYM_PTR:
	case SYM_ARRAY:
		type_tree_register(unwrapped->ctype.base_type);
		break;
	case SYM_FN:
		type_tree_register(unwrapped->ctype.base_type);
		FOR_EACH_PTR(unwrapped->arguments, argument) {
			type_tree_register(argument->ctype.base_type);
		} END_FOR_EACH_PTR(argument);
		break;
	case SYM_STRUCT:
	case SYM_UNION:
		type_registry_record(unwrapped->ident, unwrapped);
		break;
	default:
		break;
	}
}

void
type_symbols_register(struct symbol_list *symbols)
{
	struct symbol *symbol;

	FOR_EACH_PTR(symbols, symbol) {
		statistics.type_registration_symbols_visited++;
		if (symbol->namespace == NS_TYPEDEF) {
			type_registry_record(symbol->ident,
			    symbol->ctype.base_type);
		} else if (symbol->namespace == NS_STRUCT) {
			type_registry_record(symbol->ident, symbol);
		}
		type_tree_register(symbol->ctype.base_type);
	} END_FOR_EACH_PTR(symbol);
}

void
type_name_visit_types(struct ident *name, type_visit_f callback, void *data)
{
	struct type_name_to_ll_type key = {
		.name = name
	};
	struct type_name_to_ll_type *entry;
	avl_index_t where;

	(void) avl_find(&type_name_to_ll_type_index, &key, &where);
	for (entry = avl_nearest(&type_name_to_ll_type_index, where, AVL_AFTER);
	    entry != NULL && entry->name == name;
	    entry = AVL_NEXT(&type_name_to_ll_type_index, entry)) {
		if (!callback(entry->type, data))
			break;
	}
}

const struct ll_type *
type_lookup_exact(struct symbol *symbol)
{
	struct sparse_type_to_ll_type key = {
		.exact = symbol
	};
	struct sparse_type_to_ll_type *exact;

	exact = avl_find(&sparse_type_to_ll_type_index, &key, NULL);
	return (exact == NULL ? NULL : exact->type);
}

size_t
type_instance_count(const struct ll_type *type)
{
	return (type == NULL ? 0 : type->instance_count);
}

const struct type_member *
type_members(const struct ll_type *type)
{
	if (type == NULL ||
	    (type_kind(type) != SYM_STRUCT && type_kind(type) != SYM_UNION))
		return (NULL);
	return (type->members);
}

size_t
type_member_count(const struct ll_type *type)
{
	if (type == NULL ||
	    (type_kind(type) != SYM_STRUCT && type_kind(type) != SYM_UNION))
		return (0);
	return (type->member_count);
}

const struct type_member *
type_member_lookup_exact(struct symbol *symbol)
{
	struct sparse_member_to_type_member key = {
		.exact = symbol
	};
	struct sparse_member_to_type_member *exact;

	exact = avl_find(&sparse_member_to_type_member_index, &key, NULL);
	return (exact == NULL ? NULL : exact->member);
}

void
type_name_visit(struct ident *name, type_name_visit_f callback, void *data)
{
	struct type_name_to_sparse_type key = {
		.name = name
	};
	struct type_name_to_sparse_type *entry;
	avl_index_t where;

	(void) avl_find(&type_name_to_sparse_type_index, &key, &where);
	for (entry = avl_nearest(&type_name_to_sparse_type_index, where, AVL_AFTER);
	    entry != NULL && entry->name == name;
	    entry = AVL_NEXT(&type_name_to_sparse_type_index, entry)) {
		if (!callback(entry->type, data))
			break;
	}
}

void
type_registry_show(FILE *stream)
{
	struct type_name_to_sparse_type *entry;

	for (entry = avl_first(&type_name_to_sparse_type_index); entry != NULL;
	    entry = AVL_NEXT(&type_name_to_sparse_type_index, entry)) {
		struct position pos = entry->type->pos;
		const char *kind = entry->type->type == SYM_STRUCT ?
		    "struct" : "union";

		(void) fprintf(stream,
		    "type %s kind=%s source=%s:%u:%u translation-unit=%u\n",
		    show_ident(entry->name), kind, stream_name(pos.stream),
		    pos.line, pos.pos, entry->type->translation_unit);
	}
	(void) fprintf(stream, "types %zu\n",
	    avl_numnodes(&type_name_to_sparse_type_index));
}
