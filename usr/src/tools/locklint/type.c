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
	struct type_origin origin;
	size_t instance_count;
	avl_node_t by_origin;
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
type_exact_intern(struct symbol *symbol)
{
	struct exact_type key = {
		.exact = symbol
	};
	struct exact_type *exact;
	struct ll_type *type;
	avl_index_t where;

	exact = avl_find(&exact_types, &key, &where);
	if (exact != NULL)
		return (exact->type);
	type = type_origin_intern(symbol);
	exact = calloc(1, sizeof (*exact));
	if (exact == NULL)
		die("out of memory recording exact type");
	exact->exact = symbol;
	exact->type = type;
	avl_insert(&exact_types, exact, where);
	type->instance_count++;
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
	ll_type = type_exact_intern(type);
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

	statistics.type_registration_nodes_visited++;
	type = type_node_strip(type);
	if (type == NULL)
		return;
	switch (type->type) {
	case SYM_PTR:
	case SYM_ARRAY:
		type_tree_register(type->ctype.base_type);
		break;
	case SYM_FN:
		type_tree_register(type->ctype.base_type);
		FOR_EACH_PTR(type->arguments, argument) {
			type_tree_register(argument->ctype.base_type);
		} END_FOR_EACH_PTR(argument);
		break;
	case SYM_STRUCT:
	case SYM_UNION:
		type_registry_record(type->ident, type);
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
