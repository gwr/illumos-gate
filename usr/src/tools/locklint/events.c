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
#include "events.h"
#include "symbol.h"

static void
show_event_position(struct position pos, const char *event)
{
	(void) printf("  %s:%u:%u %s ", stream_name(pos.stream),
	    pos.line, pos.pos, event);
}

static const char *
call_name(struct instruction *insn)
{
	if (insn->func != NULL && insn->func->type == PSEUDO_SYM &&
	    insn->func->sym != NULL && insn->func->sym->ident != NULL)
		return (show_ident(insn->func->sym->ident));

	return ("<indirect>");
}

static struct expression *
call_argument(struct instruction *insn, unsigned int index)
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

enum locklint_lock_action
locklint_get_lock_action(struct translation_unit *tu, struct instruction *insn,
    struct locklint_access *access, enum locklint_lock_mode *mode)
{
	enum locklint_lock_action action;
	const char *name;
	unsigned int argument = 0;

	*mode = LOCKLINT_MODE_UNHELD;
	*access = (struct locklint_access){ 0 };
	if (insn->opcode != OP_CALL)
		return (LOCKLINT_LOCK_NONE);

	name = call_name(insn);
	if (strcmp(name, "mutex_enter") == 0 ||
	    strcmp(name, "mutex_lock") == 0) {
		action = LOCKLINT_LOCK_ACQUIRE;
		*mode = LOCKLINT_MODE_MUTEX;
	} else if (strcmp(name, "rw_rdlock") == 0) {
		action = LOCKLINT_LOCK_ACQUIRE;
		*mode = LOCKLINT_MODE_READER;
	} else if (strcmp(name, "rw_wrlock") == 0) {
		action = LOCKLINT_LOCK_ACQUIRE;
		*mode = LOCKLINT_MODE_WRITER;
	} else if (strcmp(name, "rw_enter") == 0) {
		struct expression *rw_mode = call_argument(insn, 1);
		unsigned long long value;

		if (rw_mode == NULL || rw_mode->type != EXPR_VALUE)
			return (LOCKLINT_LOCK_NONE);
		value = rw_mode->value;
		if (value == 0)
			*mode = LOCKLINT_MODE_WRITER;
		else if (value == 1 || value == 2)
			*mode = LOCKLINT_MODE_READER;
		else
			return (LOCKLINT_LOCK_NONE);
		action = LOCKLINT_LOCK_ACQUIRE;
	} else if (strcmp(name, "mutex_exit") == 0 ||
	    strcmp(name, "mutex_unlock") == 0 ||
	    strcmp(name, "rw_exit") == 0 ||
	    strcmp(name, "rw_unlock") == 0) {
		action = LOCKLINT_LOCK_RELEASE;
	} else if (strcmp(name, "cv_wait") == 0 ||
	    strcmp(name, "cv_wait_sig") == 0 ||
	    strcmp(name, "cv_timedwait") == 0 ||
	    strcmp(name, "cv_timedwait_sig") == 0 ||
	    strcmp(name, "cv_reltimedwait") == 0 ||
	    strcmp(name, "cv_reltimedwait_sig") == 0 ||
	    strcmp(name, "cv_wait_stop") == 0 ||
	    strcmp(name, "cv_timedwait_hires") == 0 ||
	    strcmp(name, "cv_timedwait_sig_hrtime") == 0 ||
	    strcmp(name, "cv_wait_sig_swap") == 0 ||
	    strcmp(name, "cv_wait_sig_swap_core") == 0 ||
	    strcmp(name, "cv_waituntil_sig") == 0) {
		action = LOCKLINT_LOCK_WAIT;
		*mode = LOCKLINT_MODE_MUTEX;
		argument = 1;
	} else if (strcmp(name, "rw_downgrade") == 0) {
		action = LOCKLINT_LOCK_DOWNGRADE;
		*mode = LOCKLINT_MODE_READER;
	} else {
		return (LOCKLINT_LOCK_NONE);
	}

	(void) locklint_get_call_argument_access(tu, insn, argument, access);
	return (action);
}

static bool
show_memory_event(struct translation_unit *tu, struct instruction *insn)
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

	show_event_position(insn->access->pos, event);
	locklint_show_access(stdout, insn->access);
	(void) printf(" offset=%u", insn->offset);
	if (locklint_get_instruction_access(tu, insn, &access) &&
	    locklint_data_policy(&access, &policy, &protector) &&
	    (policy.protection == LOCKLINT_PROTECTION_MUTEX ||
	    policy.protection == LOCKLINT_PROTECTION_RWLOCK)) {
		struct symbol *name = protector.member != NULL ?
		    protector.member : protector.root;

		(void) printf(" protected-by=%s",
		    name != NULL && name->ident != NULL ?
		    show_ident(name->ident) : "<unknown>");
	}
	(void) printf("\n");
	return (true);
}

static bool
show_call_event(struct translation_unit *tu, struct instruction *insn)
{
	struct locklint_access access;
	struct expression *arg;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;
	const char *event;
	char name[128];

	if (insn->opcode != OP_CALL)
		return (false);

	(void) snprintf(name, sizeof (name), "%s", call_name(insn));
	action = locklint_get_lock_action(tu, insn, &access, &mode);
	if (action == LOCKLINT_LOCK_ACQUIRE) {
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
	else
		event = "CALL";

	show_event_position(insn->call_expr != NULL ?
	    insn->call_expr->pos : insn->pos, event);
	if (event[0] == 'C') {
		(void) printf("%s\n", name);
		return (true);
	}

	arg = action == LOCKLINT_LOCK_WAIT ? call_argument(insn, 1) :
	    (insn->call_expr != NULL ?
	    first_expression(insn->call_expr->args) : NULL);
	locklint_show_access(stdout, arg);
	(void) printf("\n");
	return (true);
}

void
locklint_show_events(struct translation_unit *tu, struct entrypoint *ep)
{
	struct basic_block *bb;
	char function[128];

	(void) snprintf(function, sizeof (function), "%s",
	    ep->name->ident != NULL ? show_ident(ep->name->ident) :
	    "<anonymous>");
	(void) printf("function %s\n", function);

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;
		bool showed_block = false;

		FOR_EACH_PTR(bb->insns, insn) {
			if (insn->bb == NULL)
				continue;
			if (!showed_block &&
			    (insn->opcode == OP_LOAD ||
			    insn->opcode == OP_STORE ||
			    insn->opcode == OP_CALL)) {
				(void) printf("block .L%u\n", bb->nr);
				showed_block = true;
			}
			if (show_memory_event(tu, insn))
				continue;
			(void) show_call_event(tu, insn);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}
