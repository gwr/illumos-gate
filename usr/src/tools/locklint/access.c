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
#include <string.h>
#include "expression.h"
#include "access.h"
#include "identity.h"
#include "symbol.h"

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
	return (access->root != NULL);
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
	if (left->offset != right->offset ||
	    !same_access_object(left, right))
		return (false);
	if (left->object == NULL)
		return (left->member == right->member);
	/* Separately parsed declarations have distinct member symbols. */
	return (same_ident(left->member != NULL ? left->member->ident : NULL,
	    right->member != NULL ? right->member->ident : NULL));
}

bool
locklint_access_contains(const struct locklint_access *container,
    const struct locklint_access *access)
{
	struct expression *member;
	unsigned long suffix = 0;

	if (!same_access_object(container, access))
		return (false);
	if (container->member == NULL)
		return (true);
	if (locklint_same_access(container, access))
		return (true);
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

bool
locklint_access_base(const struct locklint_access *access,
    struct symbol *owner_type, unsigned long relative_offset,
    unsigned long *base_offset)
{
	struct expression *member;
	unsigned long suffix = 0;

	if (access->type == owner_type && access->offset == relative_offset) {
		*base_offset = 0;
		return (true);
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
