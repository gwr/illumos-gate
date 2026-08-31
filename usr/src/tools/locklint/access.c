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
 * Recover and display locklint object and member identities from Sparse
 * expressions.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cwchash/hashtable.h"
#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "identity.h"
#include "symbol.h"
#include "target.h"

struct locklint_member_path {
	struct locklint_member_path *parent;
	struct ident *ident;
	unsigned long offset;
	unsigned int depth;
};

static struct hashtable *member_paths;

static unsigned int
member_path_hash(void *key)
{
	struct locklint_member_path *path = key;
	uintptr_t parent = (uintptr_t)path->parent;
	uintptr_t ident = (uintptr_t)path->ident;
	unsigned long offset = path->offset;

	return ((unsigned int)(parent ^ (parent >> 16) ^ ident ^
	    (ident >> 16) ^ offset ^ (offset >> 16)));
}

static int
same_member_path(void *left_key, void *right_key)
{
	struct locklint_member_path *left = left_key;
	struct locklint_member_path *right = right_key;

	return (left->parent == right->parent &&
	    left->ident == right->ident &&
	    left->offset == right->offset);
}

static struct locklint_member_path *
intern_member_path(struct locklint_member_path *parent, struct ident *ident,
    unsigned long offset)
{
	struct locklint_member_path key = { 0 };
	struct locklint_member_path *path;

	if (member_paths == NULL) {
		member_paths = create_hashtable(128, member_path_hash,
		    same_member_path);
		if (member_paths == NULL)
			die("out of memory creating member path table");
	}
	key.parent = parent;
	key.ident = ident;
	key.offset = offset;
	path = hashtable_search(member_paths, &key);
	if (path != NULL)
		return (path);
	path = calloc(1, sizeof (*path));
	if (path == NULL)
		die("out of memory recording member path");
	path->parent = parent;
	path->ident = ident;
	path->offset = offset;
	path->depth = parent != NULL ? parent->depth + 1 : 1;
	if (!hashtable_insert(member_paths, path, path))
		die("out of memory interning member path");
	return (path);
}

static struct symbol *
find_root(struct expression *expr)
{
	struct symbol *sym;

	if (expr == NULL)
		return (NULL);

	switch (expr->type) {
	case EXPR_SYMBOL:
		return (expr->symbol);
	case EXPR_PREOP:
	case EXPR_POSTOP:
		return (find_root(expr->unop));
	case EXPR_BINOP:
	case EXPR_COMMA:
	case EXPR_COMPARE:
	case EXPR_LOGICAL:
	case EXPR_ASSIGNMENT:
		sym = find_root(expr->left);
		if (sym != NULL)
			return (sym);
		return (find_root(expr->right));
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		return (find_root(expr->cast_expression));
	case EXPR_SLICE:
		return (find_root(expr->base));
	default:
		return (NULL);
	}
}

static struct expression *
find_member(struct expression *expr)
{
	struct expression *member;

	if (expr == NULL)
		return (NULL);

	if (expr->member_symbol != NULL)
		return (expr);

	switch (expr->type) {
	case EXPR_PREOP:
	case EXPR_POSTOP:
		return (find_member(expr->unop));
	case EXPR_BINOP:
	case EXPR_COMMA:
	case EXPR_COMPARE:
	case EXPR_LOGICAL:
	case EXPR_ASSIGNMENT:
		member = find_member(expr->left);
		if (member != NULL)
			return (member);
		return (find_member(expr->right));
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		return (find_member(expr->cast_expression));
	case EXPR_SLICE:
		return (find_member(expr->base));
	default:
		return (NULL);
	}
}

static void
show_member(FILE *stream, struct expression *expr)
{
	struct expression *base_member;
	struct symbol *root;

	base_member = find_member(expr->member_base);
	if (base_member != NULL) {
		show_member(stream, base_member);
	} else {
		root = find_root(expr->member_base);
		(void) fprintf(stream, "%s",
		    root != NULL && root->ident != NULL ?
		    show_ident(root->ident) : "<unknown>");
	}
	(void) fprintf(stream, ".%s",
	    show_ident(expr->member_symbol->ident));
}

static unsigned long
member_offset(struct expression *expr)
{
	struct expression *member;
	unsigned long offset = 0;

	member = find_member(expr);
	while (member != NULL) {
		offset += member->member_path_offset;
		member = find_member(member->member_base);
	}
	return (offset);
}

static struct locklint_member_path *
member_path(struct expression *expr)
{
	struct expression *member = find_member(expr);
	struct locklint_member_path *parent;
	unsigned long offset;

	if (member == NULL)
		return (NULL);
	parent = member_path(member->member_base);
	offset = (parent != NULL ? parent->offset : 0) +
	    member->member_path_offset;
	return (intern_member_path(parent, member->member_symbol->ident,
	    offset));
}

static struct symbol *
root_type(struct symbol *root)
{
	struct symbol *type;

	if (root == NULL)
		return (NULL);
	type = root->ctype.base_type;
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type != NULL && type->type == SYM_PTR)
		type = type->ctype.base_type;
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	return (type);
}

static struct symbol *
compound_type(struct symbol *type)
{
	while (type != NULL && type->type == SYM_NODE)
		type = type->ctype.base_type;
	if (type == NULL ||
	    (type->type != SYM_STRUCT && type->type != SYM_UNION))
		return (NULL);
	return (type);
}

bool
locklint_get_access(struct translation_unit *tu, struct expression *expr,
    struct locklint_access *access)
{
	struct expression *member;

	access->root = find_root(expr);
	access->object = locklint_object_identity(tu, access->root);
	access->type = root_type(access->root);
	member = find_member(expr);
	access->member = member != NULL ? member->member_symbol : NULL;
	access->offset = member_offset(expr);
	access->expr = expr;
	access->path = member_path(expr);
	access->address_base = NULL;
	access->address_offset = 0;
	return (access->root != NULL);
}

static bool
add_address_offset(int64_t *offset, long long value)
{
	if ((value > 0 && *offset > INT64_MAX - value) ||
	    (value < 0 && *offset < INT64_MIN - value))
		return (false);
	*offset += value;
	return (true);
}

/*
 * Retain only exact address-preserving transformations.  Other computed
 * pseudos remain useful opaque bases: repeated uses compare equal, while
 * unrelated symbolic computations do not.
 */
static void
set_address(struct locklint_access *access, struct pseudo *pseudo,
    unsigned long offset)
{
	struct pseudo *original = pseudo;
	int64_t displacement = 0;

	if (offset > (uint64_t)INT64_MAX)
		return;
	while (pseudo != NULL && pseudo->type == PSEUDO_REG &&
	    pseudo->def != NULL) {
		struct instruction *def = pseudo->def;
		struct pseudo *next = NULL;
		long long value = 0;

		switch (def->opcode) {
		case OP_PTRCAST:
			next = def->src;
			break;
		case OP_ADD:
			if (def->src1 != NULL &&
			    def->src1->type == PSEUDO_VAL) {
				value = def->src1->value;
				next = def->src2;
			} else if (def->src2 != NULL &&
			    def->src2->type == PSEUDO_VAL) {
				value = def->src2->value;
				next = def->src1;
			}
			break;
		case OP_SUB:
			if (def->src2 != NULL &&
			    def->src2->type == PSEUDO_VAL &&
			    def->src2->value != INT64_MIN) {
				value = -def->src2->value;
				next = def->src1;
			}
			break;
		default:
			break;
		}
		if (next == NULL)
			break;
		if (!add_address_offset(&displacement, value)) {
			pseudo = original;
			displacement = 0;
			break;
		}
		pseudo = next;
	}
	if (!add_address_offset(&displacement, (long long)offset))
		return;
	access->address_base = pseudo;
	access->address_offset = displacement;
}

bool
locklint_get_instruction_access(struct translation_unit *tu,
    const struct instruction *insn, struct locklint_access *access)
{
	if (insn == NULL || insn->access == NULL ||
	    (insn->opcode != OP_LOAD && insn->opcode != OP_STORE) ||
	    !locklint_get_access(tu, insn->access, access))
		return (false);
	set_address(access, insn->addr, insn->offset);
	return (true);
}

bool
locklint_get_call_argument_access(struct translation_unit *tu,
    const struct instruction *insn, unsigned int index,
    struct locklint_access *access)
{
	struct expression *argument = NULL;
	struct pseudo *pseudo = NULL;
	struct pseudo_list *arguments;
	unsigned int current = 0;

	if (insn == NULL || insn->opcode != OP_CALL || insn->call_expr == NULL)
		return (false);
	FOR_EACH_PTR(insn->call_expr->args, argument) {
		if (current++ == index)
			break;
	} END_FOR_EACH_PTR(argument);
	if (argument == NULL)
		return (false);
	current = 0;
	arguments = insn->arguments;
	FOR_EACH_PTR(arguments, pseudo) {
		if (current++ == index)
			break;
	} END_FOR_EACH_PTR(pseudo);
	if (pseudo == NULL || !locklint_get_access(tu, argument, access))
		return (false);
	set_address(access, pseudo, 0);
	return (true);
}

static bool
same_ident(const struct ident *left, const struct ident *right)
{
	if (left == right)
		return (true);
	return (left != NULL && right != NULL &&
	    left->len == right->len &&
	    memcmp(left->name, right->name, left->len) == 0);
}

static bool
same_access_object(const struct locklint_access *left,
    const struct locklint_access *right)
{
	/* Locals and formals have no linkage and retain Sparse identity. */
	if (left->object == NULL && right->object == NULL)
		return (left->root == right->root);
	return (left->object != NULL && left->object == right->object);
}

bool
locklint_same_access(const struct locklint_access *left,
    const struct locklint_access *right)
{
	if (left->address_base != NULL && right->address_base != NULL) {
		return (left->address_base == right->address_base &&
		    left->address_offset == right->address_offset);
	}
	if (left->offset != right->offset ||
	    !same_access_object(left, right))
		return (false);
	if (left->path != NULL && right->path != NULL)
		return (left->path == right->path);
	if (left->object == NULL)
		return (left->member == right->member);
	/* Separately parsed declarations have distinct member symbols. */
	return (same_ident(left->member != NULL ? left->member->ident : NULL,
	    right->member != NULL ? right->member->ident : NULL));
}

static bool
path_contains(const struct locklint_member_path *container,
    const struct locklint_member_path *access)
{
	while (access != NULL && access->depth > container->depth)
		access = access->parent;
	return (access == container);
}

bool
locklint_access_contains(const struct locklint_access *container,
    const struct locklint_access *access)
{
	struct expression *member;
	unsigned long suffix = 0;

	if (!same_access_object(container, access))
		return (false);
	if (container->path == NULL && container->member == NULL)
		return (true);
	if (locklint_same_access(container, access))
		return (true);
	if (container->path != NULL && access->path != NULL)
		return (path_contains(container->path, access->path));
	/*
	 * Walk from the selected leaf toward its root.  Subtracting each
	 * member's relative offset recovers the absolute offset of its parent.
	 */
	for (member = find_member(access->expr); member != NULL;
	    member = find_member(member->member_base)) {
		unsigned long offset;

		if (suffix > access->offset)
			return (false);
		offset = access->offset - suffix;
		if (offset == container->offset &&
		    (container->object == NULL ?
		    member->member_symbol == container->member :
		    same_ident(member->member_symbol->ident,
		    container->member->ident)))
			return (true);
		suffix += member->member_path_offset;
	}
	return (false);
}

static struct locklint_member_path *
append_member_path(struct locklint_member_path *base,
    const struct locklint_member_path *relative, unsigned long base_offset)
{
	if (relative == NULL)
		return (base);
	base = append_member_path(base, relative->parent, base_offset);
	return (intern_member_path(base, relative->ident,
	    base_offset + relative->offset));
}

void
locklint_rebase_access(const struct locklint_access *base,
    const struct locklint_access *relative, struct locklint_access *result)
{
	*result = *base;
	result->path = append_member_path(base->path, relative->path,
	    base->offset);
	if (relative->member != NULL)
		result->member = relative->member;
	result->offset = base->offset + relative->offset;
	if (base->address_base != NULL) {
		if (relative->offset <= INT64_MAX &&
		    add_address_offset(&result->address_offset,
		    (long long)relative->offset)) {
			result->address_base = base->address_base;
		} else {
			result->address_base = NULL;
			result->address_offset = 0;
		}
	}
	result->expr = NULL;
}

unsigned int
locklint_access_depth(const struct locklint_access *access)
{
	return (access->path != NULL ? access->path->depth : 0);
}

bool
locklint_access_base(const struct locklint_access *access,
    struct symbol *owner_type, unsigned long relative_offset,
    unsigned long *base_offset)
{
	struct expression *member;
	unsigned long suffix = 0;

	if (access->type == owner_type && access->offset >= relative_offset) {
		unsigned long displacement = access->offset - relative_offset;
		int owner_size = bits_to_bytes(owner_type->bit_size);

		if (displacement == 0 ||
		    (owner_size > 0 && displacement % owner_size == 0)) {
			*base_offset = displacement;
			return (true);
		}
	}
	for (member = find_member(access->expr); member != NULL;
	    member = find_member(member->member_base)) {
		struct symbol *type;

		suffix += member->member_path_offset;
		if (suffix > access->offset)
			return (false);
		type = compound_type(member->member_base->ctype);
		if (type == owner_type && suffix == relative_offset) {
			*base_offset = access->offset - suffix;
			return (true);
		}
	}
	return (false);
}

void
locklint_access_cleanup(void)
{
	if (member_paths == NULL)
		return;
	hashtable_destroy(member_paths, 0);
	member_paths = NULL;
}

void
locklint_show_access(FILE *stream, struct expression *expr)
{
	struct expression *member;
	struct symbol *root;

	member = find_member(expr);
	if (member != NULL) {
		show_member(stream, member);
		return;
	}

	root = find_root(expr);
	(void) fprintf(stream, "%s",
	    root != NULL && root->ident != NULL ?
	    show_ident(root->ident) : "<unknown>");
}
