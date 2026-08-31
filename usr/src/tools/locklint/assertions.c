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
 * Capture lock assertions and translate their predicates into lock-state
 * assumptions.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "annotations.h"
#include "assertions.h"
#include "events.h"
#include "identity.h"
#include "token.h"

struct assertion {
	struct position invocation;
	struct translation_unit *tu;
	struct position predicate;
	struct position argument_start;
	struct position argument_end;
	unsigned int modes;
	struct assertion *next;
};

static struct assertion *assertions;
static struct assertion **assertions_tail = &assertions;

static bool
position_before(struct position left, struct position right)
{
	if (left.stream != right.stream)
		return (false);
	if (left.line != right.line)
		return (left.line < right.line);
	return (left.pos < right.pos);
}

static bool
position_in_range(struct position pos, struct position start,
    struct position end)
{
	return (pos.stream == start.stream &&
	    !position_before(pos, start) && !position_before(end, pos));
}

static const struct token *
matching_close(const struct token *open)
{
	const struct token *token;
	unsigned int depth = 1;

	for (token = open->next; !eof_token(token); token = token->next) {
		if (token_type(token) == TOKEN_SPECIAL &&
		    token->special == '(') {
			depth++;
		} else if (token_type(token) == TOKEN_SPECIAL &&
		    token->special == ')' && --depth == 0) {
			return (token);
		}
	}
	return (NULL);
}

static bool
token_is(const struct token *token, const char *name)
{
	return (token != NULL && token_type(token) == TOKEN_IDENT &&
	    strcmp(show_token(token), name) == 0);
}

static bool
predicate_modes(const struct token *token, unsigned int *positive,
    unsigned int *negative)
{
	if (token_is(token, "MUTEX_HELD") ||
	    token_is(token, "mutex_owned") ||
	    token_is(token, "_mutex_held")) {
		*positive = LOCKLINT_MODE_MUTEX;
		*negative = LOCKLINT_MODE_UNHELD;
		return (true);
	}
	if (token_is(token, "MUTEX_NOT_HELD")) {
		*positive = LOCKLINT_MODE_UNHELD;
		*negative = LOCKLINT_MODE_MUTEX;
		return (true);
	}
	if (token_is(token, "RW_READ_HELD") ||
	    token_is(token, "rw_read_held") ||
	    token_is(token, "_rw_read_held")) {
		*positive = LOCKLINT_MODE_READER;
		*negative = LOCKLINT_MODE_UNHELD | LOCKLINT_MODE_WRITER;
		return (true);
	}
	if (token_is(token, "RW_WRITE_HELD") ||
	    token_is(token, "rw_write_held") ||
	    token_is(token, "_rw_write_held")) {
		*positive = LOCKLINT_MODE_WRITER;
		*negative = LOCKLINT_MODE_UNHELD | LOCKLINT_MODE_READER;
		return (true);
	}
	if (token_is(token, "RW_LOCK_HELD") ||
	    token_is(token, "rw_lock_held")) {
		*positive = LOCKLINT_MODE_READER | LOCKLINT_MODE_WRITER;
		*negative = LOCKLINT_MODE_UNHELD;
		return (true);
	}
	return (false);
}

static bool
is_special(const struct token *token, unsigned int special)
{
	return (token != NULL && token_type(token) == TOKEN_SPECIAL &&
	    token->special == special);
}

static unsigned int
adjust_modes(const struct token *previous, const struct token *close,
    unsigned int positive, unsigned int negative)
{
	const struct token *operator = close->next;
	const struct token *value = operator != NULL ? operator->next : NULL;
	bool invert = is_special(previous, '!');

	if ((is_special(operator, SPECIAL_EQUAL) ||
	    is_special(operator, SPECIAL_NOTEQUAL)) &&
	    value != NULL && token_type(value) == TOKEN_NUMBER &&
	    strcmp(show_token(value), "0") == 0) {
		if (is_special(operator, SPECIAL_EQUAL))
			invert = !invert;
	}
	if (invert) {
		return (negative);
	}
	return (positive);
}

static int
capture_assertion(const struct token *macro, const struct token *open,
    void *data)
{
	const struct token *end;
	const struct token *previous = open;
	const struct token *token;

	(void) macro;
	(void) data;

	end = matching_close(open);
	if (end == NULL)
		return (0);
	for (token = open->next; token != end; previous = token,
	    token = token->next) {
		const struct token *predicate_open;
		const struct token *predicate_close;
		struct assertion *assertion;
		unsigned int positive;
		unsigned int negative;

		if (!predicate_modes(token, &positive, &negative))
			continue;
		predicate_open = token->next;
		if (!is_special(predicate_open, '('))
			continue;
		predicate_close = matching_close(predicate_open);
		if (predicate_close == NULL || predicate_close == predicate_open->next)
			continue;
		assertion = calloc(1, sizeof (*assertion));
		if (assertion == NULL)
			die("out of memory recording lock assertion");
		assertion->invocation = macro->pos;
		assertion->tu = locklint_translation_unit_current();
		assertion->predicate = token->pos;
		assertion->argument_start = predicate_open->next->pos;
		assertion->argument_end = predicate_close->pos;
		assertion->modes = adjust_modes(previous, predicate_close,
		    positive, negative);
		*assertions_tail = assertion;
		assertions_tail = &assertion->next;
	}
	return (1);
}

void
locklint_assertions_enable(void)
{
	add_macro_expansion_hook("ASSERT", capture_assertion, NULL);
	add_macro_expansion_hook("VERIFY", capture_assertion, NULL);
	/*
	 * synch.h defines this predicate as 1.  Keep existing assertions but
	 * lower their preserved argument to the ordinary no-competition event.
	 */
	add_pre_buffer("#strong_define NO_COMPETING_THREADS "
	    "__context__(0, 0, %lu);\n",
	    (unsigned long)LOCKLINT_EXECUTION_NO_COMPETITION);
}

static bool
is_lock_predicate(struct instruction *insn)
{
	const char *name;

	if (insn->opcode != OP_CALL || insn->func == NULL ||
	    insn->func->type != PSEUDO_SYM || insn->func->sym == NULL ||
	    insn->func->sym->ident == NULL)
		return (false);
	name = show_ident(insn->func->sym->ident);
	return (strcmp(name, "mutex_owned") == 0 ||
	    strcmp(name, "_mutex_held") == 0 ||
	    strcmp(name, "rw_read_held") == 0 ||
	    strcmp(name, "_rw_read_held") == 0 ||
	    strcmp(name, "rw_write_held") == 0 ||
	    strcmp(name, "_rw_write_held") == 0 ||
	    strcmp(name, "rw_lock_held") == 0);
}

bool
locklint_get_assertion(struct translation_unit *tu, struct instruction *insn,
    struct locklint_access *access, unsigned int *modes)
{
	struct expression *argument;
	struct assertion *assertion;
	unsigned int selected = 0;

	*modes = 0;
	*access = (struct locklint_access){ 0 };
	if (!is_lock_predicate(insn) || insn->call_expr == NULL)
		return (false);
	argument = first_expression(insn->call_expr->args);
	if (argument == NULL)
		return (false);
	for (assertion = assertions; assertion != NULL;
	    assertion = assertion->next) {
		bool invocation_match;

		/* Identical header positions in different translations are distinct. */
		if (assertion->tu != tu)
			continue;
		if (assertion->predicate.stream != argument->pos.stream)
			continue;
		if (position_in_range(argument->pos,
		    assertion->argument_start, assertion->argument_end) ||
		    (insn->call_expr->pos.line == assertion->predicate.line &&
		    insn->call_expr->pos.pos == assertion->predicate.pos)) {
			selected = assertion->modes;
			break;
		}
		invocation_match =
		    insn->call_expr->pos.line == assertion->invocation.line &&
		    insn->call_expr->pos.pos == assertion->invocation.pos;
		if (!invocation_match)
			continue;
		if (selected != 0 && selected != assertion->modes)
			return (false);
		selected = assertion->modes;
	}
	if (selected == 0 ||
	    !locklint_get_access(tu, argument, access))
		return (false);
	*modes = selected;
	return (true);
}
