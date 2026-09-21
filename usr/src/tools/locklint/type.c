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
 * same source origin, while Sparse's backend-owned aux field caches exact
 * type mappings and indexes preserve exact-member and name lookup.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"
#include "diagnostics.h"
#include "expression.h"
#include "lib.h"
#include "statistics.h"
#include "symbol.h"
#include "type.h"

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

struct pending_type_mapping {
	struct symbol *exact;
	struct ll_type *type;
	avl_node_t by_exact;
};

struct pending_member_mapping {
	struct symbol *exact;
	struct type_member *member;
	struct pending_member_mapping *next;
};

struct type_validation {
	avl_tree_t types_by_exact;
	struct pending_member_mapping *members;
	struct symbol *mismatch;
	const char *reason;
};

struct compared_types {
	const struct ll_type *left;
	const struct ll_type *right;
	avl_node_t by_pair;
};

static avl_tree_t types_by_origin;
static avl_tree_t types_by_shape;
static avl_tree_t sparse_member_to_type_member_index;
static avl_tree_t type_name_to_ll_type_index;
static bool type_registry_is_consistent;
static struct symbol *type_registry_mismatch;
static const char *type_registry_mismatch_reason;
static struct position type_registry_mismatch_position;

static enum type
type_kind(const struct ll_type *type)
{
	enum type kind = type->representative->type;

	return (kind == SYM_TYPEDEF ? SYM_NODE : kind);
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

	statistics.type_registry_comparisons++;
	result = AVL_PCMP(left->name, right->name);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->type, right->type));
}

static int
pending_type_compare(const void *left_arg, const void *right_arg)
{
	const struct pending_type_mapping *left = left_arg;
	const struct pending_type_mapping *right = right_arg;

	return (AVL_PCMP(left->exact, right->exact));
}

static int
compared_types_compare(const void *left_arg, const void *right_arg)
{
	const struct compared_types *left = left_arg;
	const struct compared_types *right = right_arg;
	int result;

	result = AVL_PCMP(left->left, right->left);
	if (result != 0)
		return (result);
	return (AVL_PCMP(left->right, right->right));
}

void
type_registry_create(void)
{
	avl_create(&types_by_origin, type_origin_compare,
	    sizeof (struct ll_type), offsetof(struct ll_type, by_origin));
	avl_create(&types_by_shape, type_shape_compare,
	    sizeof (struct ll_type), offsetof(struct ll_type, by_shape));
	avl_create(&sparse_member_to_type_member_index,
	    sparse_member_to_type_member_compare,
	    sizeof (struct sparse_member_to_type_member),
	    offsetof(struct sparse_member_to_type_member, by_exact));
	avl_create(&type_name_to_ll_type_index, type_name_to_ll_type_compare,
	    sizeof (struct type_name_to_ll_type),
	    offsetof(struct type_name_to_ll_type, by_name));
	type_registry_is_consistent = true;
	type_registry_mismatch = NULL;
	type_registry_mismatch_reason = NULL;
	type_registry_mismatch_position = (struct position){ 0 };
}

void
type_registry_destroy(void)
{
	struct type_name_to_ll_type *name;
	struct sparse_member_to_type_member *member;
	struct ll_type *type;
	void *cookie = NULL;

	while ((name = avl_destroy_nodes(&type_name_to_ll_type_index,
	    &cookie)) != NULL)
		free(name);
	avl_destroy(&type_name_to_ll_type_index);
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
type_exact_find(struct symbol *symbol)
{
	return (symbol == NULL ? NULL : symbol->aux);
}

static void
type_exact_record(struct symbol *symbol, struct ll_type *type)
{
	if (symbol->aux != NULL)
		return;
	symbol->aux = type;
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

static struct type_member *
type_member_find(struct symbol *symbol)
{
	struct sparse_member_to_type_member key = {
		.exact = symbol
	};
	struct sparse_member_to_type_member *exact;

	exact = avl_find(&sparse_member_to_type_member_index, &key, NULL);
	return (exact == NULL ? NULL : exact->member);
}

static unsigned long
type_semantic_modifiers(const struct symbol *exact)
{
	return (exact->ctype.modifiers & ~MOD_IGNORE);
}

static void
type_validation_create(struct type_validation *validation)
{
	avl_create(&validation->types_by_exact, pending_type_compare,
	    sizeof (struct pending_type_mapping),
	    offsetof(struct pending_type_mapping, by_exact));
	validation->members = NULL;
	validation->mismatch = NULL;
	validation->reason = NULL;
}

static void
type_validation_destroy(struct type_validation *validation)
{
	struct pending_type_mapping *type;
	struct pending_member_mapping *member;
	void *cookie = NULL;

	while ((type = avl_destroy_nodes(&validation->types_by_exact,
	    &cookie)) != NULL)
		free(type);
	avl_destroy(&validation->types_by_exact);
	while ((member = validation->members) != NULL) {
		validation->members = member->next;
		free(member);
	}
}

static bool
type_validation_mismatch(struct type_validation *validation,
    struct symbol *exact, const char *reason)
{
	if (validation->reason == NULL) {
		validation->mismatch = exact;
		validation->reason = reason;
	}
	return (false);
}

/*
 * Add a tentative exact-to-locklint mapping before descending through the
 * candidate.  Existing mappings terminate recursive type graphs.
 */
static int
type_validation_enter(struct type_validation *validation,
    struct symbol *exact, struct ll_type *type)
{
	struct pending_type_mapping key = {
		.exact = exact
	};
	struct pending_type_mapping *mapping;
	avl_index_t where;

	mapping = avl_find(&validation->types_by_exact, &key, &where);
	if (mapping != NULL)
		return (mapping->type == type ? 0 : -1);
	mapping = calloc(1, sizeof (*mapping));
	if (mapping == NULL)
		die("out of memory validating type");
	mapping->exact = exact;
	mapping->type = type;
	avl_insert(&validation->types_by_exact, mapping, where);
	return (1);
}

static void
type_validation_add_member(struct type_validation *validation,
    struct symbol *exact, struct type_member *member)
{
	struct pending_member_mapping *mapping;

	mapping = calloc(1, sizeof (*mapping));
	if (mapping == NULL)
		die("out of memory validating type member");
	mapping->exact = exact;
	mapping->member = member;
	mapping->next = validation->members;
	validation->members = mapping;
}

static bool type_validate_exact(struct type_validation *, struct symbol *,
    struct ll_type *, bool);

static bool
type_validate_member(struct type_validation *validation,
    struct symbol *exact, struct type_member *member)
{
	struct symbol *representative = member->representative;

	if (exact->ident != representative->ident ||
	    exact->offset != representative->offset ||
	    exact->bit_size != representative->bit_size ||
	    exact->bit_offset != representative->bit_offset ||
	    is_bitfield_type(exact) != is_bitfield_type(representative))
		return (type_validation_mismatch(validation, exact,
		    "member layout differs"));
	if (!type_validate_exact(validation, exact->ctype.base_type,
	    member->type, false))
		return (false);
	type_validation_add_member(validation, exact, member);
	return (true);
}

static bool
type_validate_struct(struct type_validation *validation, struct symbol *exact,
    struct ll_type *type)
{
	struct symbol *member;
	size_t index = 0;

	if (ptr_list_size((struct ptr_list *)exact->symbol_list) !=
	    type->member_count)
		return (type_validation_mismatch(validation, exact,
		    "member count differs"));
	FOR_EACH_PTR(exact->symbol_list, member) {
		if (!type_validate_member(validation, member,
		    &type->members[index++]))
			return (false);
	} END_FOR_EACH_PTR(member);
	return (true);
}

static bool
type_validate_union(struct type_validation *validation, struct symbol *exact,
    struct ll_type *type)
{
	struct symbol *member;
	bool *matched;
	size_t count = 0;

	if (ptr_list_size((struct ptr_list *)exact->symbol_list) !=
	    type->member_count)
		return (type_validation_mismatch(validation, exact,
		    "member count differs"));
	matched = calloc(type->member_count, sizeof (*matched));
	if (matched == NULL && type->member_count != 0)
		die("out of memory validating union");
	FOR_EACH_PTR(exact->symbol_list, member) {
		size_t index;

		for (index = 0; index < type->member_count; index++) {
			if (!matched[index] && member->ident ==
			    type->members[index].representative->ident)
				break;
		}
		if (index == type->member_count ||
		    !type_validate_member(validation, member,
		    &type->members[index])) {
			if (index == type->member_count)
				(void) type_validation_mismatch(validation, member,
				    "union member differs");
			free(matched);
			return (false);
		}
		matched[index] = true;
		count++;
	} END_FOR_EACH_PTR(member);
	free(matched);
	return (count == type->member_count);
}

static bool
type_validate_enum(struct symbol *exact, struct symbol *representative)
{
	size_t count;
	size_t index;

	if (exact->ctype.base_type != representative->ctype.base_type)
		return (false);
	count = ptr_list_size((struct ptr_list *)exact->symbol_list);
	if (count != ptr_list_size(
	    (struct ptr_list *)representative->symbol_list))
		return (false);
	for (index = 0; index < count; index++) {
		struct symbol *left = ptr_list_nth_entry(
		    (struct ptr_list *)exact->symbol_list, index);
		struct symbol *right = ptr_list_nth_entry(
		    (struct ptr_list *)representative->symbol_list, index);

		if (left->ident != right->ident ||
		    left->initializer == NULL || right->initializer == NULL ||
		    left->initializer->value != right->initializer->value)
			return (false);
	}
	return (true);
}

static bool
type_validate_origin(struct symbol *exact, struct ll_type *type)
{
	struct position left = exact->pos;
	struct position right = type->representative->pos;

	return (exact->type == type_kind(type) &&
	    strcmp(stream_name(left.stream), stream_name(right.stream)) == 0 &&
	    left.line == right.line && left.pos == right.pos);
}

static bool
type_node_is_transparent(struct symbol *exact)
{
	struct symbol *base = exact->ctype.base_type;
	unsigned long modifiers;

	if (base == NULL)
		return (false);
	modifiers = type_semantic_modifiers(exact) &
	    (MOD_QUALIFIER | MOD_SAFE | MOD_BITWISE | MOD_NOCAST |
	    MOD_NODEREF | MOD_NORETURN);
	return (modifiers == 0 && exact->ctype.as == NULL &&
	    (exact->ctype.alignment == 0 ||
	    exact->ctype.alignment == base->ctype.alignment) &&
	    (exact->bit_size == 0 || exact->bit_size == base->bit_size));
}

static unsigned long
type_node_modifiers(struct symbol *exact)
{
	unsigned long modifiers = 0;

	while (exact != NULL &&
	    (exact->type == SYM_NODE || exact->type == SYM_TYPEDEF)) {
		modifiers |= type_semantic_modifiers(exact) &
		    (MOD_QUALIFIER | MOD_SAFE | MOD_BITWISE | MOD_NOCAST |
		    MOD_NODEREF | MOD_NORETURN);
		exact = exact->ctype.base_type;
	}
	return (modifiers);
}

static struct ident *
type_node_address_space(struct symbol *exact)
{
	struct ident *address_space = NULL;

	while (exact != NULL &&
	    (exact->type == SYM_NODE || exact->type == SYM_TYPEDEF)) {
		if (address_space == NULL && exact->ctype.as != NULL)
			address_space = exact->ctype.as;
		exact = exact->ctype.base_type;
	}
	return (address_space);
}

/*
 * An incomplete aggregate has no layout to compare.  It may stand in for the
 * corresponding named aggregate only as a pointer target; callers control
 * that context with allow_incomplete.
 */
static bool
type_aggregate_is_incomplete(struct symbol *type)
{
	return ((type->type == SYM_STRUCT || type->type == SYM_UNION) &&
	    type->endpos.line == 0 && type->symbol_list == NULL &&
	    type->bit_size == 0);
}

static bool
type_incomplete_pointer_target_matches(struct symbol *exact,
    struct ll_type *type)
{
	struct symbol *representative = type->representative;

	if ((exact->type != SYM_STRUCT && exact->type != SYM_UNION) ||
	    exact->type != type_kind(type) || exact->ident == NULL ||
	    exact->ident != representative->ident)
		return (false);
	return (type_aggregate_is_incomplete(exact) ||
	    type_aggregate_is_incomplete(representative));
}

/*
 * Compare one exact Sparse type with an existing locklint type.  Tentative
 * mappings are added before recursive edges are followed, so recursive
 * aggregates terminate without publishing an unvalidated mapping.
 */
static bool
type_validate_exact(struct type_validation *validation, struct symbol *exact,
    struct ll_type *type, bool allow_incomplete)
{
	struct ll_type *mapped;
	struct symbol *argument;
	size_t index = 0;
	int entered;

	if (exact == NULL || type == NULL)
		return (exact == NULL && type == NULL ? true :
		    type_validation_mismatch(validation, exact,
		    "referenced type is incomplete"));
	if (allow_incomplete &&
	    type_incomplete_pointer_target_matches(exact, type))
		return (true);
	mapped = type_exact_find(exact);
	if (mapped != NULL)
		return (mapped == type);
	examine_symbol_type(exact);

	if (exact->type == SYM_BITFIELD) {
		entered = type_validation_enter(validation, exact, type);
		if (entered <= 0)
			return (entered == 0);
		return (type_validate_exact(validation,
		    exact->ctype.base_type, type, allow_incomplete));
	}
	if ((exact->type == SYM_NODE || exact->type == SYM_TYPEDEF) &&
	    type_node_is_transparent(exact)) {
		entered = type_validation_enter(validation, exact, type);
		if (entered <= 0)
			return (entered == 0);
		return (type_validate_exact(validation,
		    exact->ctype.base_type, type, allow_incomplete));
	}
	if (exact->type != type_kind(type))
		return (type_validation_mismatch(validation, exact,
		    "type kind differs"));
	entered = type_validation_enter(validation, exact, type);
	if (entered <= 0)
		return (entered == 0);
	if ((exact->type == SYM_NODE || exact->type == SYM_TYPEDEF ?
	    type_node_modifiers(exact) : type_semantic_modifiers(exact)) !=
	    type->modifiers ||
	    exact->ctype.alignment !=
	    type->representative->ctype.alignment ||
	    exact->bit_size != type->representative->bit_size ||
	    (exact->type == SYM_NODE || exact->type == SYM_TYPEDEF ?
	    type_node_address_space(exact) : exact->ctype.as) !=
	    type->address_space)
		return (type_validation_mismatch(validation, exact,
		    "type size, alignment, or qualifiers differ"));

	switch (exact->type) {
	case SYM_BASETYPE:
		return ((exact->ctype.base_type != NULL ?
		    exact->ctype.base_type : exact) ==
		    (type->representative->ctype.base_type != NULL ?
		    type->representative->ctype.base_type :
		    type->representative));
	case SYM_NODE:
	case SYM_TYPEDEF:
		mapped = type_exact_find(type->representative->ctype.base_type);
		if (mapped == NULL)
			mapped = type->base;
		return (type_validate_exact(validation,
		    exact->ctype.base_type, mapped, allow_incomplete));
	case SYM_PTR:
		return (type_validate_exact(validation,
		    exact->ctype.base_type, type->base, true));
	case SYM_ARRAY:
		return (type_validate_exact(validation,
		    exact->ctype.base_type, type->base, false));
	case SYM_FN:
		if (exact->variadic != type->representative->variadic ||
		    ptr_list_size((struct ptr_list *)exact->arguments) !=
		    type->argument_count ||
		    !type_validate_exact(validation, exact->ctype.base_type,
		    type->base, false))
			return (false);
		FOR_EACH_PTR(exact->arguments, argument) {
			if (!type_validate_exact(validation, argument,
			    type->arguments[index++], false))
				return (false);
		} END_FOR_EACH_PTR(argument);
		return (true);
	case SYM_STRUCT:
	case SYM_UNION:
	case SYM_ENUM:
		if (!type_validate_origin(exact, type))
			return (false);
		if (exact->type == SYM_STRUCT)
			return (type_validate_struct(validation, exact, type));
		if (exact->type == SYM_UNION)
			return (type_validate_union(validation, exact, type));
		return (type_validate_enum(exact, type->representative));
	default:
		return (type_validation_mismatch(validation, exact,
		    "unsupported type differs"));
	}
}

static void
type_validation_publish(struct type_validation *validation)
{
	struct pending_type_mapping *type;
	struct pending_member_mapping *member;

	for (type = avl_first(&validation->types_by_exact); type != NULL;
	    type = AVL_NEXT(&validation->types_by_exact, type))
		type_exact_record(type->exact, type->type);
	for (member = validation->members; member != NULL;
	    member = member->next)
		type_member_record(member->exact, member->member);
}

static bool
type_repeated_definition_record(struct symbol *exact, struct ll_type *type)
{
	struct type_validation validation;
	bool matches;

	type_validation_create(&validation);
	matches = type_validate_exact(&validation, exact, type, false);
	if (matches) {
		type_validation_publish(&validation);
	} else {
		type_registry_mismatch = exact;
		type_registry_mismatch_reason = validation.reason != NULL ?
		    validation.reason : "definition differs";
		type_registry_mismatch_position =
		    validation.mismatch != NULL ?
		    validation.mismatch->pos : exact->pos;
	}
	type_validation_destroy(&validation);
	return (matches);
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
	if (!type_registry_is_consistent)
		return (NULL);
	type = type_exact_find(exact);
	if (type != NULL)
		return (type);
	examine_symbol_type(exact);

	if (type_has_origin(exact->type)) {
		type = type_origin_intern(exact, &created);
		if (created) {
			type_exact_record(exact, type);
			type_members_build(type, exact);
		} else if (!type_repeated_definition_record(exact, type)) {
			type_registry_is_consistent = false;
			return (NULL);
		}
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

static bool type_layout_equal_recurse(avl_tree_t *, const struct ll_type *,
    const struct ll_type *);

static bool
type_member_equal(avl_tree_t *compared, const struct type_member *left,
    const struct type_member *right)
{
	struct symbol *left_exact = left->representative;
	struct symbol *right_exact = right->representative;

	return (left_exact->ident == right_exact->ident &&
	    left_exact->offset == right_exact->offset &&
	    left_exact->bit_size == right_exact->bit_size &&
	    left_exact->bit_offset == right_exact->bit_offset &&
	    is_bitfield_type(left_exact) == is_bitfield_type(right_exact) &&
	    type_layout_equal_recurse(compared, left->type, right->type));
}

static bool
type_union_equal(avl_tree_t *compared, const struct ll_type *left,
    const struct ll_type *right)
{
	bool *matched;
	size_t left_index;

	matched = calloc(right->member_count, sizeof (*matched));
	if (matched == NULL && right->member_count != 0)
		die("out of memory comparing union types");
	for (left_index = 0; left_index < left->member_count; left_index++) {
		size_t right_index;

		for (right_index = 0; right_index < right->member_count;
		    right_index++) {
			if (!matched[right_index] &&
			    left->members[left_index].representative->ident ==
			    right->members[right_index].representative->ident)
				break;
		}
		if (right_index == right->member_count ||
		    !type_member_equal(compared, &left->members[left_index],
		    &right->members[right_index])) {
			free(matched);
			return (false);
		}
		matched[right_index] = true;
	}
	free(matched);
	return (true);
}

/*
 * Compare canonical type graphs without considering aggregate origins.
 * Remember pairs before descending so recursive pointer graphs terminate.
 */
static bool
type_layout_equal_recurse(avl_tree_t *compared, const struct ll_type *left,
    const struct ll_type *right)
{
	struct compared_types key = {
		.left = left,
		.right = right
	};
	struct compared_types *pair;
	size_t index;
	avl_index_t where;

	if (left == right)
		return (true);
	if (left == NULL || right == NULL ||
	    type_kind(left) != type_kind(right) ||
	    left->modifiers != right->modifiers ||
	    left->address_space != right->address_space ||
	    left->representative->ctype.alignment !=
	    right->representative->ctype.alignment ||
	    left->representative->bit_size != right->representative->bit_size)
		return (false);
	pair = avl_find(compared, &key, &where);
	if (pair != NULL)
		return (true);
	pair = calloc(1, sizeof (*pair));
	if (pair == NULL)
		die("out of memory comparing type layouts");
	pair->left = left;
	pair->right = right;
	avl_insert(compared, pair, where);

	switch (type_kind(left)) {
	case SYM_BASETYPE:
		return ((left->representative->ctype.base_type != NULL ?
		    left->representative->ctype.base_type :
		    left->representative) ==
		    (right->representative->ctype.base_type != NULL ?
		    right->representative->ctype.base_type :
		    right->representative));
	case SYM_NODE:
	case SYM_PTR:
	case SYM_ARRAY:
		return (type_layout_equal_recurse(compared, left->base,
		    right->base));
	case SYM_FN:
		if (left->representative->variadic !=
		    right->representative->variadic ||
		    left->argument_count != right->argument_count ||
		    !type_layout_equal_recurse(compared, left->base,
		    right->base))
			return (false);
		for (index = 0; index < left->argument_count; index++) {
			if (!type_layout_equal_recurse(compared,
			    left->arguments[index], right->arguments[index]))
				return (false);
		}
		return (true);
	case SYM_STRUCT:
		if (left->member_count != right->member_count)
			return (false);
		for (index = 0; index < left->member_count; index++) {
			if (!type_member_equal(compared, &left->members[index],
			    &right->members[index]))
				return (false);
		}
		return (true);
	case SYM_UNION:
		if (left->member_count != right->member_count)
			return (false);
		return (type_union_equal(compared, left, right));
	case SYM_ENUM:
		return (type_validate_enum(left->representative,
		    right->representative));
	default:
		return (false);
	}
}

bool
type_layout_equal(const struct ll_type *left, const struct ll_type *right)
{
	avl_tree_t compared;
	struct compared_types *pair;
	void *cookie = NULL;
	bool equal;

	avl_create(&compared, compared_types_compare,
	    sizeof (struct compared_types),
	    offsetof(struct compared_types, by_pair));
	equal = type_layout_equal_recurse(&compared, left, right);
	while ((pair = avl_destroy_nodes(&compared, &cookie)) != NULL)
		free(pair);
	avl_destroy(&compared);
	return (equal);
}

static void
type_name_record(struct ident *name, struct ll_type *type)
{
	struct type_name_to_ll_type key = {
		.name = name,
		.type = type
	};
	struct type_name_to_ll_type *entry;
	struct type_name_to_ll_type *same_name;
	struct type_name_to_ll_type lower = {
		.name = name
	};
	avl_index_t name_where;
	avl_index_t where;

	statistics.type_registry_find++;
	entry = avl_find(&type_name_to_ll_type_index, &key, &where);
	if (entry != NULL) {
		statistics.type_registry_duplicates++;
		return;
	}
	(void) avl_find(&type_name_to_ll_type_index, &lower, &name_where);
	for (same_name = avl_nearest(&type_name_to_ll_type_index, name_where,
	    AVL_AFTER);
	    same_name != NULL && same_name->name == name;
	    same_name = AVL_NEXT(&type_name_to_ll_type_index, same_name)) {
		const char *kind;

		if (type_layout_equal(same_name->type, type))
			continue;
		kind = type_kind(type) == SYM_STRUCT ? "struct" : "union";
		locklint_warning(LOCKLINT_DIAG_TYPE_NAME_LAYOUT,
		    type->representative->pos,
		    "%s '%s' has the same name but a different layout",
		    kind, show_ident(name));
		break;
	}
	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		die("out of memory recording type name");
	entry->name = name;
	entry->type = type;
	avl_insert(&type_name_to_ll_type_index, entry, where);
	statistics.type_registry_insertions++;
}

static void
type_registry_record(struct ident *name, struct symbol *type)
{
	struct ll_type *ll_type;

	if (name == NULL)
		return;
	type = type_compound_resolve(type);
	if (type == NULL)
		return;
	ll_type = type_intern(type);
	if (ll_type != NULL && !type_aggregate_is_incomplete(type))
		type_name_record(name, ll_type);
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
	return (type_exact_find(symbol));
}

struct symbol *
type_representative(const struct ll_type *type)
{
	return (type == NULL ? NULL : type->representative);
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
	return (type_member_find(symbol));
}

bool
type_registry_consistent(void)
{
	return (type_registry_is_consistent);
}

void
type_registry_report_errors(void)
{
	struct symbol *type = type_registry_mismatch;
	const char *kind;

	if (type == NULL)
		return;
	kind = type->type == SYM_STRUCT ? "struct" :
	    type->type == SYM_UNION ? "union" : "enum";
	sparse_error(type_registry_mismatch_position,
	    "locklint: %s '%s' is inconsistently defined: %s",
	    kind, show_ident(type->ident), type_registry_mismatch_reason);
}

void
type_registry_show(FILE *stream)
{
	struct type_name_to_ll_type *entry;

	for (entry = avl_first(&type_name_to_ll_type_index); entry != NULL;
	    entry = AVL_NEXT(&type_name_to_ll_type_index, entry)) {
		struct position pos = entry->type->representative->pos;
		const char *kind = type_kind(entry->type) == SYM_STRUCT ?
		    "struct" : type_kind(entry->type) == SYM_UNION ?
		    "union" : "enum";

		(void) fprintf(stream,
		    "type %s kind=%s source=%s:%u:%u instances=%zu\n",
		    show_ident(entry->name), kind, stream_name(pos.stream),
		    pos.line, pos.pos, entry->type->instance_count);
	}
	(void) fprintf(stream, "types %zu\n",
	    avl_numnodes(&type_name_to_ll_type_index));
}
