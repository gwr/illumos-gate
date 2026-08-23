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

enum locklint_lock_action
locklint_get_lock_action(struct instruction *insn,
    struct locklint_access *access)
{
	struct expression *arg;
	enum locklint_lock_action action;
	const char *name;

	access->root = NULL;
	access->member = NULL;
	if (insn->opcode != OP_CALL)
		return (LOCKLINT_LOCK_NONE);

	name = call_name(insn);
	if (strcmp(name, "mutex_enter") == 0)
		action = LOCKLINT_LOCK_ACQUIRE;
	else if (strcmp(name, "mutex_exit") == 0)
		action = LOCKLINT_LOCK_RELEASE;
	else
		return (LOCKLINT_LOCK_NONE);

	arg = insn->call_expr != NULL ?
	    first_expression(insn->call_expr->args) : NULL;
	(void) locklint_get_access(arg, access);
	return (action);
}

static bool
show_memory_event(struct instruction *insn)
{
	struct locklint_access access;
	struct symbol *protector;
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
	if (locklint_get_access(insn->access, &access) &&
	    access.member != NULL &&
	    (protector = locklint_protecting_member(access.member)) != NULL) {
		(void) printf(" protected-by=%s", show_ident(protector->ident));
	}
	(void) printf("\n");
	return (true);
}

static bool
show_call_event(struct instruction *insn)
{
	struct locklint_access access;
	struct expression *arg;
	enum locklint_lock_action action;
	const char *event;
	char name[128];

	if (insn->opcode != OP_CALL)
		return (false);

	(void) snprintf(name, sizeof (name), "%s", call_name(insn));
	action = locklint_get_lock_action(insn, &access);
	if (action == LOCKLINT_LOCK_ACQUIRE)
		event = "ACQUIRE";
	else if (action == LOCKLINT_LOCK_RELEASE)
		event = "RELEASE";
	else
		event = "CALL";

	show_event_position(insn->call_expr != NULL ?
	    insn->call_expr->pos : insn->pos, event);
	if (event[0] == 'C') {
		(void) printf("%s\n", name);
		return (true);
	}

	arg = insn->call_expr != NULL ?
	    first_expression(insn->call_expr->args) : NULL;
	locklint_show_access(stdout, arg);
	(void) printf("\n");
	return (true);
}

void
locklint_show_events(struct entrypoint *ep)
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
			if (show_memory_event(insn))
				continue;
			(void) show_call_event(insn);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}
