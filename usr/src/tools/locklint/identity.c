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
 * Track translation-unit provenance and canonical C object identity.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "cwchash/hashtable.h"
#include "lib.h"
#include "symbol.h"
#include "identity.h"

struct object_origin {
	struct translation_unit *tu;
	struct symbol *symbol;
	struct object_origin *next;
};

struct object_identity {
	struct ident *ident;
	/* NULL for external objects; internal objects belong to one TU. */
	struct translation_unit *tu;
	/* Keep one Sparse symbol for names, types, and future diagnostics. */
	struct symbol *representative;
	/* Preserve every declaration that contributed to this identity. */
	struct object_origin *origins;
	struct object_identity *next;
};

struct translation_unit {
	/* The top-level input, not the path of each included source file. */
	const char *file;
	/* Distinguish repeated parses of the same input pathname. */
	unsigned int id;
	/* File-scope static objects are canonical only within this parse. */
	struct object_identity *internal_objects;
	struct hashtable *internal_by_ident;
	struct translation_unit *next;
};

static struct translation_unit *translation_units;
static struct translation_unit **translation_units_tail = &translation_units;
static struct translation_unit *current_translation_unit;
static struct object_identity *external_objects;
static struct hashtable *external_by_ident;
static struct hashtable *objects_by_symbol;
static unsigned int next_translation_unit_id;

static unsigned int
pointer_hash(void *pointer)
{
	uintptr_t value = (uintptr_t)pointer;

	return ((unsigned int)(value ^ (value >> 16)));
}

static int
same_pointer(void *left, void *right)
{
	return (left == right);
}

static struct hashtable *
new_pointer_table(unsigned int size)
{
	struct hashtable *table;

	table = create_hashtable(size, pointer_hash, same_pointer);
	if (table == NULL)
		die("out of memory creating object identity table");
	return (table);
}

static bool
function_symbol(const struct symbol *symbol)
{
	const struct symbol *type;

	/*
	 * Functions follow definition and call-resolution rules, not object
	 * access identity, and are represented separately by function_info.
	 */
	if (symbol == NULL)
		return (false);
	type = symbol->ctype.base_type;
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type != NULL && type->type == SYM_FN);
}

static struct object_identity *
find_object(struct hashtable *objects, const struct ident *ident)
{
	/*
	 * Sparse interns identifiers for the lifetime of this invocation, so an
	 * ident pointer is a stable spelling key across translation units.
	 */
	return (hashtable_search(objects, (void *)ident));
}

static void
record_origin(struct object_identity *object, struct translation_unit *tu,
    struct symbol *symbol)
{
	struct object_origin *origin;
	struct object_identity *bound;

	bound = hashtable_search(objects_by_symbol, symbol);
	if (bound != NULL && bound != object)
		die("one declaration has conflicting object identities");
	if (bound == NULL &&
	    !hashtable_insert(objects_by_symbol, symbol, object))
		die("out of memory binding object declaration");
	for (origin = object->origins; origin != NULL; origin = origin->next) {
		if (origin->tu == tu && origin->symbol == symbol)
			return;
	}
	origin = calloc(1, sizeof (*origin));
	if (origin == NULL)
		die("out of memory recording object origin");
	origin->tu = tu;
	origin->symbol = symbol;
	origin->next = object->origins;
	object->origins = origin;
}

static struct object_identity *
add_object(struct object_identity **objects, struct hashtable *objects_by_ident,
    struct translation_unit *owner, struct translation_unit *origin_tu,
    struct symbol *symbol)
{
	struct object_identity *object;

	object = calloc(1, sizeof (*object));
	if (object == NULL)
		die("out of memory recording object identity");
	object->ident = symbol->ident;
	object->tu = owner;
	object->representative = symbol;
	object->next = *objects;
	*objects = object;
	if (!hashtable_insert(objects_by_ident, symbol->ident, object))
		die("out of memory indexing object identity");
	record_origin(object, origin_tu, symbol);
	return (object);
}

struct translation_unit *
locklint_translation_unit_begin(const char *file)
{
	struct translation_unit *tu;

	tu = calloc(1, sizeof (*tu));
	if (tu == NULL)
		die("out of memory recording translation unit");
	tu->file = file;
	tu->id = next_translation_unit_id++;
	tu->internal_by_ident = new_pointer_table(64);
	if (objects_by_symbol == NULL) {
		objects_by_symbol = new_pointer_table(1024);
		external_by_ident = new_pointer_table(1024);
	}
	*translation_units_tail = tu;
	translation_units_tail = &tu->next;
	current_translation_unit = tu;
	return (tu);
}

struct translation_unit *
locklint_translation_unit_current(void)
{
	return (current_translation_unit);
}

unsigned int
locklint_translation_unit_id(const struct translation_unit *tu)
{
	return (tu->id);
}

const char *
locklint_translation_unit_file(const struct translation_unit *tu)
{
	return (tu->file);
}

/*
 * Decide whether a declaration may name an internal-linkage object or
 * function already registered for this translation unit.
 *
 * At file scope, the translation-unit table has already established that the
 * identifier has internal linkage.  Sparse does not always retain the earlier
 * static declaration on a later extern's declaration chain.
 *
 * At block scope, Sparse's next_id chain starts with the declaration visible
 * at the extern declaration.  C inherits that declaration's linkage.  Sparse
 * removes declarations from its active symbol table when leaving a scope,
 * but does not rewrite this retained declaration chain, so it remains the
 * visibility snapshot from binding time.  A no-linkage local therefore stops
 * the search and prevents a hidden file-static declaration from being
 * selected.
 */
bool
locklint_symbol_can_use_internal(const struct symbol *symbol)
{
	const struct symbol *previous;
	unsigned long modifiers;

	if (symbol == NULL)
		return (false);
	modifiers = symbol->ctype.modifiers;
	if ((modifiers & (MOD_TOPLEVEL | MOD_STATIC)) != 0)
		return (true);
	if ((modifiers & MOD_EXTERN) == 0)
		return (false);

	for (previous = symbol->next_id; previous != NULL;
	    previous = previous->next_id) {
		if (previous->namespace != symbol->namespace)
			continue;
		if ((previous->ctype.modifiers & MOD_NONLOCAL) == 0)
			return (false);
		return (locklint_symbol_can_use_internal(previous));
	}
	return (false);
}

void
locklint_translation_unit_register(struct translation_unit *tu,
    struct symbol_list *symbols)
{
	struct symbol *symbol;

	/*
	 * Build the internal-object table before resolving annotations.
	 * Later extern declarations can then recover inherited internal linkage
	 * even when their own Sparse symbol records only extern storage.
	 */
	FOR_EACH_PTR(symbols, symbol) {
		unsigned long modifiers = symbol->ctype.modifiers;

		if (symbol->ident == NULL || function_symbol(symbol) ||
		    (modifiers & (MOD_TOPLEVEL | MOD_STATIC)) !=
		    (MOD_TOPLEVEL | MOD_STATIC))
			continue;
		if (find_object(tu->internal_by_ident, symbol->ident) == NULL)
			(void) add_object(&tu->internal_objects,
			    tu->internal_by_ident, tu, tu, symbol);
	} END_FOR_EACH_PTR(symbol);
}

struct object_identity *
locklint_object_identity(struct translation_unit *tu, struct symbol *symbol)
{
	struct object_identity *object;
	unsigned long modifiers;

	if (tu == NULL || symbol == NULL || symbol->ident == NULL ||
	    function_symbol(symbol))
		return (NULL);

	/*
	 * Most source instructions reuse a declaration already classified
	 * during registration or an earlier access.  Cache that decision by
	 * Sparse symbol so hot analysis paths do not repeat linkage lookup or
	 * origin scans.
	 */
	object = hashtable_search(objects_by_symbol, symbol);
	if (object != NULL)
		return (object);

	modifiers = symbol->ctype.modifiers;
	if ((modifiers & MOD_NONLOCAL) == 0)
		return (NULL);

	/*
	 * Sparse retains a later extern as a distinct declaration symbol.
	 * Resolve it against this translation unit's static-object table before
	 * considering it externally linked.
	 */
	object = find_object(tu->internal_by_ident, symbol->ident);
	if (object != NULL && locklint_symbol_can_use_internal(symbol)) {
		record_origin(object, tu, symbol);
		return (object);
	}

	if ((modifiers & MOD_STATIC) != 0) {
		if ((modifiers & MOD_TOPLEVEL) != 0)
			return (add_object(&tu->internal_objects,
			    tu->internal_by_ident, tu, tu, symbol));
		return (NULL);
	}

	/* External declarations with the same C identifier name one object. */
	object = find_object(external_by_ident, symbol->ident);
	if (object == NULL)
		return (add_object(&external_objects, external_by_ident, NULL,
		    tu, symbol));
	record_origin(object, tu, symbol);
	return (object);
}
