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

struct registered_type {
	struct ident *name;
	struct symbol *type;
	avl_node_t by_name;
};

struct type_origin {
	const char *file;
	unsigned int line;
	unsigned int column;
};

/*
 * Use ll_type rather than the overly generic "type" because this module
 * also works extensively with exact Sparse types.  Related names retain
 * type as their normal module prefix.
 */
struct ll_type {
	enum type kind;
	unsigned long modifiers;
	unsigned long alignment;
	int bit_size;
	struct ident *address_space;
	size_t instance_count;
	union {
		struct type_origin origin;
		struct {
			struct symbol *class;
		} basic;
		struct {
			struct ll_type *base;
		} derived;
		struct {
			struct ll_type *result;
			struct ll_type **arguments;
			size_t argument_count;
			bool variadic;
		} function;
	};
	avl_node_t by_origin;
	avl_node_t by_shape;
};

struct exact_type {
	struct symbol *exact;
	struct ll_type *type;
	avl_node_t by_exact;
};

struct type_name {
	struct ident *name;
	struct ll_type *type;
	avl_node_t by_name;
};

static avl_tree_t registered_types;
static avl_tree_t types_by_origin;
static avl_tree_t types_by_shape;
static avl_tree_t exact_types;
static avl_tree_t type_names;

static int
type_registry_compare(const void *left_arg, const void *right_arg)
{
	const struct registered_type *left = left_arg;
	const struct registered_type *right = right_arg;
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
	int result;

	result = AVL_CMP(left->kind, right->kind);
	if (result != 0)
		return (result);
	result = strcmp(left->origin.file, right->origin.file);
	if (result != 0)
		return (AVL_ISIGN(result));
	result = AVL_CMP(left->origin.line, right->origin.line);
	if (result != 0)
		return (result);
	return (AVL_CMP(left->origin.column, right->origin.column));
}

static int
type_shape_compare(const void *left_arg, const void *right_arg)
{
	const struct ll_type *left = left_arg;
	const struct ll_type *right = right_arg;
	size_t index;
	int result;

	result = AVL_CMP(left->kind, right->kind);
	if (result != 0)
		return (result);
	result = AVL_CMP(left->modifiers, right->modifiers);
	if (result != 0)
		return (result);
	result = AVL_CMP(left->alignment, right->alignment);
	if (result != 0)
		return (result);
	result = AVL_CMP(left->bit_size, right->bit_size);
	if (result != 0)
		return (result);
	result = AVL_PCMP(left->address_space, right->address_space);
	if (result != 0)
		return (result);

	switch (left->kind) {
	case SYM_BASETYPE:
		return (AVL_PCMP(left->basic.class, right->basic.class));
	case SYM_NODE:
	case SYM_PTR:
	case SYM_ARRAY:
		return (AVL_PCMP(left->derived.base, right->derived.base));
	case SYM_FN:
		result = AVL_CMP(left->function.variadic,
		    right->function.variadic);
		if (result != 0)
			return (result);
		result = AVL_PCMP(left->function.result,
		    right->function.result);
		if (result != 0)
			return (result);
		result = AVL_CMP(left->function.argument_count,
		    right->function.argument_count);
		if (result != 0)
			return (result);
		for (index = 0; index < left->function.argument_count; index++) {
			result = AVL_PCMP(left->function.arguments[index],
			    right->function.arguments[index]);
			if (result != 0)
				return (result);
		}
		return (0);
	default:
		die("unsupported type shape kind %d", left->kind);
	}
}

static int
exact_type_compare(const void *left_arg, const void *right_arg)
{
	const struct exact_type *left = left_arg;
	const struct exact_type *right = right_arg;

	return (AVL_PCMP(left->exact, right->exact));
}

static int
type_name_compare(const void *left_arg, const void *right_arg)
{
	const struct type_name *left = left_arg;
	const struct type_name *right = right_arg;
	int result;

	result = AVL_PCMP(left->name, right->name);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->type, right->type));
}

void
type_registry_create(void)
{
	avl_create(&registered_types, type_registry_compare,
	    sizeof (struct registered_type),
	    offsetof(struct registered_type, by_name));
	avl_create(&types_by_origin, type_origin_compare,
	    sizeof (struct ll_type), offsetof(struct ll_type, by_origin));
	avl_create(&types_by_shape, type_shape_compare,
	    sizeof (struct ll_type), offsetof(struct ll_type, by_shape));
	avl_create(&exact_types, exact_type_compare,
	    sizeof (struct exact_type), offsetof(struct exact_type, by_exact));
	avl_create(&type_names, type_name_compare,
	    sizeof (struct type_name), offsetof(struct type_name, by_name));
}

void
type_registry_destroy(void)
{
	struct registered_type *entry;
	struct type_name *name;
	struct exact_type *exact;
	struct ll_type *type;
	void *cookie = NULL;

	while ((entry = avl_destroy_nodes(&registered_types, &cookie)) != NULL)
		free(entry);
	avl_destroy(&registered_types);
	cookie = NULL;
	while ((name = avl_destroy_nodes(&type_names, &cookie)) != NULL)
		free(name);
	avl_destroy(&type_names);
	cookie = NULL;
	while ((exact = avl_destroy_nodes(&exact_types, &cookie)) != NULL)
		free(exact);
	avl_destroy(&exact_types);
	cookie = NULL;
	while ((type = avl_destroy_nodes(&types_by_shape, &cookie)) != NULL) {
		if (type->kind == SYM_FN)
			free(type->function.arguments);
		free(type);
	}
	avl_destroy(&types_by_shape);
	cookie = NULL;
	while ((type = avl_destroy_nodes(&types_by_origin, &cookie)) != NULL) {
		free((char *)type->origin.file);
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
type_origin_intern(struct symbol *exact)
{
	struct position pos = exact->pos;
	struct ll_type key = {
		.kind = exact->type,
		.origin = {
			.file = stream_name(pos.stream),
			.line = pos.line,
			.column = pos.pos
		}
	};
	struct ll_type *type;
	avl_index_t where;

	type = avl_find(&types_by_origin, &key, &where);
	if (type != NULL)
		return (type);
	type = calloc(1, sizeof (*type));
	if (type == NULL)
		die("out of memory recording type");
	type->origin.file = strdup(key.origin.file);
	if (type->origin.file == NULL)
		die("out of memory recording type origin");
	type->kind = key.kind;
	type->origin.line = key.origin.line;
	type->origin.column = key.origin.column;
	avl_insert(&types_by_origin, type, where);
	return (type);
}

static struct ll_type *
type_exact_find(struct symbol *symbol, avl_index_t *where)
{
	struct exact_type key = {
		.exact = symbol
	};
	struct exact_type *exact;

	exact = avl_find(&exact_types, &key, where);
	return (exact == NULL ? NULL : exact->type);
}

static void
type_exact_record(struct symbol *symbol, struct ll_type *type)
{
	struct exact_type key = {
		.exact = symbol
	};
	struct exact_type *exact;
	avl_index_t where;

	exact = avl_find(&exact_types, &key, &where);
	if (exact != NULL)
		return;
	exact = calloc(1, sizeof (*exact));
	if (exact == NULL)
		die("out of memory recording exact type");
	exact->exact = symbol;
	exact->type = type;
	avl_insert(&exact_types, exact, where);
	type->instance_count++;
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
		if (key->kind == SYM_FN)
			free(key->function.arguments);
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
 * Normalize Sparse's transparent node chain into one qualified type.  Each
 * inner node is interned first so every exact Sparse node still receives a
 * translation entry.
 */
static struct ll_type *
type_node_intern(struct symbol *exact)
{
	struct ll_type key = {
		.kind = SYM_NODE,
		.modifiers = type_semantic_modifiers(exact) &
		    (MOD_QUALIFIER | MOD_SAFE | MOD_BITWISE | MOD_NOCAST |
		    MOD_NODEREF | MOD_NORETURN),
		.alignment = exact->ctype.alignment,
		.bit_size = exact->bit_size,
		.address_space = exact->ctype.as
	};
	struct ll_type *base_type;

	base_type = type_intern(exact->ctype.base_type);
	if (base_type == NULL)
		return (NULL);
	if (base_type->kind == SYM_NODE) {
		key.modifiers |= base_type->modifiers;
		if (key.address_space == NULL)
			key.address_space = base_type->address_space;
		if (base_type->alignment > key.alignment)
			key.alignment = base_type->alignment;
		key.derived.base = base_type->derived.base;
	} else {
		key.derived.base = base_type;
	}
	if (key.alignment == 0)
		key.alignment = base_type->alignment;
	key.bit_size = exact->bit_size != 0 ?
	    exact->bit_size : base_type->bit_size;
	if (key.modifiers == 0 && key.address_space == NULL &&
	    key.alignment == base_type->alignment &&
	    key.bit_size == base_type->bit_size)
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
		.kind = SYM_FN,
		.modifiers = type_semantic_modifiers(exact),
		.alignment = exact->ctype.alignment,
		.bit_size = exact->bit_size,
		.address_space = exact->ctype.as,
		.function = {
			.variadic = exact->variadic
		}
	};
	struct symbol *argument;
	size_t index = 0;

	key.function.result = type_intern(exact->ctype.base_type);
	if (key.function.result == NULL)
		return (NULL);
	key.function.argument_count = ptr_list_size(
	    (struct ptr_list *)exact->arguments);
	if (key.function.argument_count != 0) {
		key.function.arguments = calloc(key.function.argument_count,
		    sizeof (*key.function.arguments));
		if (key.function.arguments == NULL)
			die("out of memory recording function type");
	}
	FOR_EACH_PTR(exact->arguments, argument) {
		key.function.arguments[index++] = type_intern(argument);
		if (key.function.arguments[index - 1] == NULL) {
			free(key.function.arguments);
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
	struct ll_type key = { 0 };
	struct ll_type *type;

	if (exact == NULL)
		return (NULL);
	type = type_exact_find(exact, NULL);
	if (type != NULL)
		return (type);
	examine_symbol_type(exact);

	if (type_has_origin(exact->type)) {
		type = type_origin_intern(exact);
	} else {
		key.kind = exact->type;
		key.modifiers = type_semantic_modifiers(exact);
		key.alignment = exact->ctype.alignment;
		key.bit_size = exact->bit_size;
		key.address_space = exact->ctype.as;
		switch (exact->type) {
		case SYM_BASETYPE:
			key.basic.class = exact->ctype.base_type != NULL ?
			    exact->ctype.base_type : exact;
			type = type_shape_intern(&key);
			break;
		case SYM_NODE:
		case SYM_TYPEDEF:
			type = type_node_intern(exact);
			break;
		case SYM_PTR:
		case SYM_ARRAY:
			key.derived.base =
			    type_intern(exact->ctype.base_type);
			if (key.derived.base == NULL)
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
	struct type_name key = {
		.name = name,
		.type = type
	};
	struct type_name *entry;
	avl_index_t where;

	entry = avl_find(&type_names, &key, &where);
	if (entry != NULL)
		return;
	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory recording type name");
	entry->name = name;
	entry->type = type;
	avl_insert(&type_names, entry, where);
}

static void
type_registry_record(struct ident *name, struct symbol *type)
{
	struct registered_type key;
	struct registered_type *entry;
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
	entry = avl_find(&registered_types, &key, &where);
	if (entry != NULL) {
		statistics.type_registry_duplicates++;
		return;
	}
	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory recording named type");
	entry->name = name;
	entry->type = type;
	avl_insert(&registered_types, entry, where);
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
	struct type_name key = {
		.name = name
	};
	struct type_name *entry;
	avl_index_t where;

	(void) avl_find(&type_names, &key, &where);
	for (entry = avl_nearest(&type_names, where, AVL_AFTER);
	    entry != NULL && entry->name == name;
	    entry = AVL_NEXT(&type_names, entry)) {
		if (!callback(entry->type, data))
			break;
	}
}

const struct ll_type *
type_lookup_exact(struct symbol *symbol)
{
	struct exact_type key = {
		.exact = symbol
	};
	struct exact_type *exact;

	exact = avl_find(&exact_types, &key, NULL);
	return (exact == NULL ? NULL : exact->type);
}

size_t
type_instance_count(const struct ll_type *type)
{
	return (type == NULL ? 0 : type->instance_count);
}

void
type_name_visit(struct ident *name, type_name_visit_f callback, void *data)
{
	struct registered_type key = {
		.name = name
	};
	struct registered_type *entry;
	avl_index_t where;

	(void) avl_find(&registered_types, &key, &where);
	for (entry = avl_nearest(&registered_types, where, AVL_AFTER);
	    entry != NULL && entry->name == name;
	    entry = AVL_NEXT(&registered_types, entry)) {
		if (!callback(entry->type, data))
			break;
	}
}

void
type_registry_show(FILE *stream)
{
	struct registered_type *entry;

	for (entry = avl_first(&registered_types); entry != NULL;
	    entry = AVL_NEXT(&registered_types, entry)) {
		struct position pos = entry->type->pos;
		const char *kind = entry->type->type == SYM_STRUCT ?
		    "struct" : "union";

		(void) fprintf(stream,
		    "type %s kind=%s source=%s:%u:%u translation-unit=%u\n",
		    show_ident(entry->name), kind, stream_name(pos.stream),
		    pos.line, pos.pos, entry->type->translation_unit);
	}
	(void) fprintf(stream, "types %zu\n",
	    avl_numnodes(&registered_types));
}
