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
 * namespace bindings.  Exact name and Sparse type identities are interned in
 * one AVL tree, which also groups entries for later lookup by name.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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

static avl_tree_t registered_types;

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

void
type_registry_create(void)
{
	avl_create(&registered_types, type_registry_compare,
	    sizeof (struct registered_type),
	    offsetof(struct registered_type, by_name));
}

void
type_registry_destroy(void)
{
	struct registered_type *entry;
	void *cookie = NULL;

	while ((entry = avl_destroy_nodes(&registered_types, &cookie)) != NULL)
		free(entry);
	avl_destroy(&registered_types);
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

static void
type_registry_record(struct ident *name, struct symbol *type)
{
	struct registered_type key;
	struct registered_type *entry;
	avl_index_t where;

	if (name == NULL)
		return;
	type = type_compound_resolve(type);
	if (type == NULL)
		return;
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
