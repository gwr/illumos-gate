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
 * Classify source-level data regions for protection auditing.  Datum keys
 * retain stable analysis identities and numeric coordinates; collection and
 * rendering are deliberately separate.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "access.h"
#include "function_info.h"
#include "identity.h"
#include "lib.h"
#include "linearize.h"
#include "protection_audit.h"
#include "symbol.h"
#include "type.h"

static int
compare_uintptr(uintptr_t left, uintptr_t right)
{
	if (left < right)
		return (-1);
	if (left > right)
		return (1);
	return (0);
}

static int
compare_ulong(unsigned long left, unsigned long right)
{
	if (left < right)
		return (-1);
	if (left > right)
		return (1);
	return (0);
}

static int
compare_uint64(uint64_t left, uint64_t right)
{
	if (left < right)
		return (-1);
	if (left > right)
		return (1);
	return (0);
}

static int
compare_string(const char *left, const char *right)
{
	if (left == right)
		return (0);
	if (left == NULL)
		return (-1);
	if (right == NULL)
		return (1);
	return (strcmp(left, right));
}

static int
compare_region(const struct protection_audit_datum_key *left,
    const struct protection_audit_datum_key *right)
{
	int comparison;

	comparison = locklint_member_path_compare(left->path, right->path);
	if (comparison != 0)
		return (comparison);
	comparison = compare_ulong(left->offset, right->offset);
	if (comparison != 0)
		return (comparison);
	return (compare_uint64(left->size, right->size));
}

static int
compare_tu(const struct translation_unit *left,
    const struct translation_unit *right)
{
	int comparison;

	if (left == right)
		return (0);
	if (left == NULL)
		return (-1);
	if (right == NULL)
		return (1);
	comparison = compare_string(locklint_translation_unit_file(left),
	    locklint_translation_unit_file(right));
	if (comparison != 0)
		return (comparison);
	if (locklint_translation_unit_id(left) <
	    locklint_translation_unit_id(right))
		return (-1);
	if (locklint_translation_unit_id(left) >
	    locklint_translation_unit_id(right))
		return (1);
	return (0);
}

/*
 * An instruction access whose lowered base is its declaration names the
 * declaration's storage itself.  A non-symbol base represents a dereference
 * or derived address and must not be rejected merely because its source root
 * is a local pointer.
 */
static bool
direct_thread_private(const struct locklint_access *access)
{
	struct symbol *declared_type;

	if ((access->root->ctype.modifiers & MOD_TLS) != 0)
		return (true);
	if (access->address_base != NULL)
		return (access->address_base_is_symbol);

	declared_type = type_node_strip(access->root->ctype.base_type);
	return (declared_type == NULL || declared_type->type != SYM_PTR);
}

/*
 * Select the strongest identity available without inventing provenance.
 * Exact objects and local statics retain declaration identity; indirect
 * aggregate accesses use their canonical type and member path.  Other
 * indirect accesses remain available for later provenance and site keys.
 */
enum protection_audit_key_result
protection_audit_datum_key_init(struct protection_audit_datum_key *key,
    struct translation_unit *tu, const struct function_info *function,
    const struct locklint_access *access)
{
	const struct translation_unit *owner;
	struct symbol *compound;
	uint64_t size;

	if (key == NULL || tu == NULL || access == NULL ||
	    access->root == NULL || !locklint_access_size(access, &size))
		return (PROTECTION_AUDIT_KEY_UNSUPPORTED);
	(void) memset(key, 0, sizeof (*key));

	if ((access->root->ctype.modifiers & MOD_TLS) != 0)
		return (PROTECTION_AUDIT_KEY_EXCLUDED);

	if (access->object != NULL) {
		owner = locklint_object_owner(access->object);
		key->kind = owner == NULL ? PROTECTION_AUDIT_DATUM_EXTERNAL :
		    PROTECTION_AUDIT_DATUM_INTERNAL;
		key->object = access->object;
	} else if ((access->root->ctype.modifiers & MOD_STATIC) != 0) {
		if (function == NULL || function->tu != tu ||
		    function->ep == NULL ||
		    function->ep->name == NULL)
			return (PROTECTION_AUDIT_KEY_UNSUPPORTED);
		key->kind = PROTECTION_AUDIT_DATUM_LOCAL_STATIC;
		key->tu = tu;
		key->function = function;
		key->symbol = access->root;
	} else {
		if (direct_thread_private(access))
			return (PROTECTION_AUDIT_KEY_EXCLUDED);
		compound = type_compound_resolve(access->type);
		key->type = type_lookup_exact(compound);
		if (key->type == NULL || type_report_order(key->type) == 0)
			return (PROTECTION_AUDIT_KEY_UNSUPPORTED);
		key->kind = PROTECTION_AUDIT_DATUM_STRUCTURAL;
	}

	key->path = access->path;
	key->offset = access->offset;
	key->size = size;
	return (PROTECTION_AUDIT_KEY_OK);
}

/*
 * Identity comparison is suitable for the owning datum index.  It uses
 * canonical analysis pointers rather than display text, then distinguishes
 * regions within that identity.
 */
int
protection_audit_datum_identity_compare(
    const struct protection_audit_datum_key *left,
    const struct protection_audit_datum_key *right)
{
	int comparison;

	if (left->kind < right->kind)
		return (-1);
	if (left->kind > right->kind)
		return (1);
	switch (left->kind) {
	case PROTECTION_AUDIT_DATUM_EXTERNAL:
	case PROTECTION_AUDIT_DATUM_INTERNAL:
		comparison = compare_uintptr((uintptr_t)left->object,
		    (uintptr_t)right->object);
		break;
	case PROTECTION_AUDIT_DATUM_STRUCTURAL:
		comparison = compare_uintptr((uintptr_t)left->type,
		    (uintptr_t)right->type);
		break;
	case PROTECTION_AUDIT_DATUM_LOCAL_STATIC:
		comparison = compare_uintptr((uintptr_t)left->tu,
		    (uintptr_t)right->tu);
		if (comparison == 0)
			comparison = compare_uintptr((uintptr_t)left->function,
			    (uintptr_t)right->function);
		if (comparison == 0)
			comparison = compare_uintptr((uintptr_t)left->symbol,
			    (uintptr_t)right->symbol);
		break;
	default:
		abort();
	}
	if (comparison != 0)
		return (comparison);
	comparison = compare_uintptr((uintptr_t)left->path,
	    (uintptr_t)right->path);
	if (comparison != 0)
		return (comparison);
	comparison = compare_ulong(left->offset, right->offset);
	if (comparison != 0)
		return (comparison);
	return (compare_uint64(left->size, right->size));
}

/*
 * Report comparison follows the user-visible source hierarchy.  The identity
 * comparator is only a final total-order tie-breaker after every retained
 * deterministic source coordinate compares equal.
 */
int
protection_audit_datum_report_compare(
    const struct protection_audit_datum_key *left,
    const struct protection_audit_datum_key *right)
{
	int comparison;
	size_t left_order;
	size_t right_order;

	if (left->kind < right->kind)
		return (-1);
	if (left->kind > right->kind)
		return (1);
	switch (left->kind) {
	case PROTECTION_AUDIT_DATUM_EXTERNAL:
		comparison = compare_string(locklint_object_name(left->object),
		    locklint_object_name(right->object));
		break;
	case PROTECTION_AUDIT_DATUM_INTERNAL:
		comparison = compare_tu(locklint_object_owner(left->object),
		    locklint_object_owner(right->object));
		if (comparison == 0)
			comparison = compare_string(
			    locklint_object_name(left->object),
			    locklint_object_name(right->object));
		break;
	case PROTECTION_AUDIT_DATUM_STRUCTURAL:
		left_order = type_report_order(left->type);
		right_order = type_report_order(right->type);
		if (left_order < right_order)
			return (-1);
		if (left_order > right_order)
			return (1);
		comparison = compare_uintptr((uintptr_t)left->type,
		    (uintptr_t)right->type);
		break;
	case PROTECTION_AUDIT_DATUM_LOCAL_STATIC:
		comparison = compare_tu(left->tu, right->tu);
		if (comparison == 0) {
			struct symbol *left_function = left->function->ep->name;
			struct symbol *right_function = right->function->ep->name;

			if (left_function->pos.stream <
			    right_function->pos.stream)
				comparison = -1;
			else if (left_function->pos.stream >
			    right_function->pos.stream)
				comparison = 1;
			else if (left_function->pos.line <
			    right_function->pos.line)
				comparison = -1;
			else if (left_function->pos.line >
			    right_function->pos.line)
				comparison = 1;
			else if (left_function->pos.pos <
			    right_function->pos.pos)
				comparison = -1;
			else if (left_function->pos.pos >
			    right_function->pos.pos)
				comparison = 1;
			else
				comparison = compare_string(
				    show_ident(left_function->ident),
				    show_ident(right_function->ident));
		}
		if (comparison == 0) {
			if (left->symbol->pos.stream < right->symbol->pos.stream)
				comparison = -1;
			else if (left->symbol->pos.stream >
			    right->symbol->pos.stream)
				comparison = 1;
			else if (left->symbol->pos.line <
			    right->symbol->pos.line)
				comparison = -1;
			else if (left->symbol->pos.line >
			    right->symbol->pos.line)
				comparison = 1;
			else if (left->symbol->pos.pos <
			    right->symbol->pos.pos)
				comparison = -1;
			else if (left->symbol->pos.pos >
			    right->symbol->pos.pos)
				comparison = 1;
			else
				comparison = compare_string(
				    show_ident(left->symbol->ident),
				    show_ident(right->symbol->ident));
		}
		break;
	default:
		abort();
	}
	if (comparison != 0)
		return (comparison);
	comparison = compare_region(left, right);
	if (comparison != 0)
		return (comparison);
	return (protection_audit_datum_identity_compare(left, right));
}
