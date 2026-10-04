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
 * Decode and display memory, call, and locking events from Sparse
 * instructions.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "expression.h"
#include "linearize.h"
#include "access.h"
#include "annotations.h"
#include "assertions.h"
#include "events.h"
#include "sync_api.h"
#include "symbol.h"

static void
show_event_position(FILE *stream, struct position pos, const char *event)
{
	(void) fprintf(stream, "  %s:%u:%u %s ", stream_name(pos.stream),
	    pos.line, pos.pos, event);
}

static const char *
call_name(const struct instruction *insn)
{
	if (insn->func != NULL && insn->func->type == PSEUDO_SYM &&
	    insn->func->sym != NULL && insn->func->sym->ident != NULL)
		return (show_ident(insn->func->sym->ident));

	return ("<indirect>");
}

static struct expression *
call_argument(const struct instruction *insn, unsigned int index)
{
	struct expression *argument;
	unsigned int current = 0;

	if (insn->call_expr == NULL)
		return (NULL);
	FOR_EACH_PTR(insn->call_expr->args, argument) {
		if (current++ == index)
			return (argument);
	} END_FOR_EACH_PTR(argument);
	return (NULL);
}

/*
 * Decode the public krw_t constants used by rw_enter() and rw_tryenter().
 * Both reader variants provide the same ownership mode to locklint.
 */
static bool
call_rw_mode(const struct instruction *insn, unsigned int argument,
    enum locklint_lock_mode *mode)
{
	struct expression *expression = call_argument(insn, argument);
	unsigned long long value;

	if (expression == NULL || expression->type != EXPR_VALUE)
		return (false);
	value = expression->value;
	if (value == 0)
		*mode = LOCKLINT_MODE_WRITER;
	else if (value == 1 || value == 2)
		*mode = LOCKLINT_MODE_READER;
	else
		return (false);
	return (true);
}

enum locklint_lock_action
locklint_get_lock_action(struct translation_unit *tu,
    const struct instruction *insn, struct locklint_access *access,
    enum locklint_lock_mode *mode)
{
	const struct sync_func *func;
	const char *name;

	*mode = LOCKLINT_MODE_UNHELD;
	*access = (struct locklint_access){ 0 };
	if (insn->opcode != OP_CALL)
		return (LOCKLINT_LOCK_NONE);

	name = call_name(insn);
	func = sync_api_find(name);
	if (func == NULL)
		return (LOCKLINT_LOCK_NONE);
	*mode = func->mode;
	if (func->mode_argument != LOCKLINT_NO_ARGUMENT &&
	    !call_rw_mode(insn, func->mode_argument, mode))
		return (LOCKLINT_LOCK_NONE);
	(void) locklint_get_call_argument_access(tu, insn,
	    func->lock_argument, access);
	return (func->action);
}

static bool
show_memory_event(FILE *stream, struct translation_unit *tu,
    struct instruction *insn)
{
	struct locklint_access access;
	struct locklint_access protector;
	struct locklint_data_policy policy;
	const char *event;

	if (insn->access == NULL)
		return (false);
	if (insn->opcode == OP_LOAD)
		event = "READ";
	else if (insn->opcode == OP_STORE)
		event = "WRITE";
	else
		return (false);

	show_event_position(stream, insn->access->pos, event);
	locklint_show_access(stream, insn->access);
	(void) fprintf(stream, " offset=%u", insn->offset);
	if (locklint_get_instruction_access(tu, insn, &access) &&
	    locklint_data_policy(&access, &policy, &protector) &&
	    (policy.protection == LOCKLINT_PROTECTION_MUTEX ||
	    policy.protection == LOCKLINT_PROTECTION_RWLOCK ||
	    policy.protection == LOCKLINT_PROTECTION_LOCK_ROLE)) {
		struct symbol *name = protector.member != NULL ?
		    protector.member : protector.root;

		(void) fprintf(stream, " protected-by=%s",
		    name != NULL && name->ident != NULL ?
		    show_ident(name->ident) : "<unknown>");
	}
	(void) fprintf(stream, "\n");
	return (true);
}

static bool
show_call_event(FILE *stream, struct translation_unit *tu,
    struct instruction *insn)
{
	struct locklint_access access;
	struct expression *arg;
	const struct sync_func *func;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;
	const char *event;
	char name[128];

	if (insn->opcode != OP_CALL)
		return (false);
	if (locklint_is_assertion_consumer(insn))
		return (true);

	(void) snprintf(name, sizeof (name), "%s", call_name(insn));
	action = locklint_get_lock_action(tu, insn, &access, &mode);
	if (action == LOCKLINT_LOCK_ACQUIRE ||
	    action == LOCKLINT_LOCK_RESULT_ACQUIRE) {
		if (mode == LOCKLINT_MODE_READER)
			event = "ACQUIRE-READ";
		else if (mode == LOCKLINT_MODE_WRITER)
			event = "ACQUIRE-WRITE";
		else
			event = "ACQUIRE";
	}
	else if (action == LOCKLINT_LOCK_RELEASE)
		event = "RELEASE";
	else if (action == LOCKLINT_LOCK_WAIT)
		event = "WAIT";
	else if (action == LOCKLINT_LOCK_DOWNGRADE)
		event = "DOWNGRADE";
	else if (action == LOCKLINT_LOCK_TRY_ACQUIRE ||
	    action == LOCKLINT_LOCK_TRY_ACQUIRE_ZERO) {
		if (mode == LOCKLINT_MODE_READER)
			event = "TRY-ACQUIRE-READ";
		else if (mode == LOCKLINT_MODE_WRITER)
			event = "TRY-ACQUIRE-WRITE";
		else
			event = "TRY-ACQUIRE";
	}
	else if (action == LOCKLINT_LOCK_TRY_UPGRADE)
		event = "TRY-UPGRADE";
	else
		event = "CALL";

	show_event_position(stream, insn->call_expr != NULL ?
	    insn->call_expr->pos : insn->pos, event);
	if (event[0] == 'C') {
		(void) fprintf(stream, "%s\n", name);
		return (true);
	}

	func = sync_api_find(name);
	arg = func != NULL ?
	    call_argument(insn, func->lock_argument) :
	    (insn->call_expr != NULL ?
	    first_expression(insn->call_expr->args) : NULL);
	locklint_show_access(stream, arg);
	(void) fprintf(stream, "\n");
	return (true);
}

void
locklint_show_events(FILE *stream, struct translation_unit *tu,
    struct entrypoint *ep)
{
	struct basic_block *bb;
	char function[128];

	(void) snprintf(function, sizeof (function), "%s",
	    ep->name->ident != NULL ? show_ident(ep->name->ident) :
	    "<anonymous>");
	(void) fprintf(stream, "function %s\n", function);

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;
		bool showed_block = false;

		FOR_EACH_PTR(bb->insns, insn) {
			if (insn->bb == NULL)
				continue;
			if (!showed_block &&
			    (insn->opcode == OP_LOAD ||
			    insn->opcode == OP_STORE ||
			    (insn->opcode == OP_CALL &&
			    !locklint_is_assertion_consumer(insn)))) {
				(void) fprintf(stream, "block .L%u\n", bb->nr);
				showed_block = true;
			}
			if (show_memory_event(stream, tu, insn))
				continue;
			(void) show_call_event(stream, tu, insn);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}
