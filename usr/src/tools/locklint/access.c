#include <stdio.h>
#include "expression.h"
#include "access.h"
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

	if (expr->member_ident != NULL)
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
	(void) fprintf(stream, ".%s", show_ident(expr->member_ident));
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
