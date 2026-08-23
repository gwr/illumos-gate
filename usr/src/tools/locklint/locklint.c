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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "expression.h"
#include "lib.h"
#include "linearize.h"
#include "access.h"
#include "annotations.h"
#include "check.h"
#include "events.h"
#include "parse.h"
#include "symbol.h"

static bool dump_parsed;
static bool dump_linearized;
static bool dump_accesses;
static bool dump_annotations;
static bool dump_events;
static bool check_locks;

/*
 * Effectively force -nostdinc for now
 */
void
locklint_init_include_path(void)
{
}

static void
usage(FILE *stream)
{
	(void) fprintf(stream,
	    "usage: locklint [--check-locks] [--dump-parsed] "
	    "[--dump-linearized] "
	    "[--dump-accesses] [--dump-annotations] [--dump-events] "
	    "[sparse-options] file.c ...\n");
}

static int
options(int argc, char **argv)
{
	int dst = 1;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--check-locks") == 0) {
			check_locks = true;
		} else if (strcmp(argv[i], "--dump-parsed") == 0) {
			dump_parsed = true;
		} else if (strcmp(argv[i], "--dump-linearized") == 0) {
			dump_linearized = true;
		} else if (strcmp(argv[i], "--dump-accesses") == 0) {
			dump_accesses = true;
		} else if (strcmp(argv[i], "--dump-annotations") == 0) {
			dump_annotations = true;
		} else if (strcmp(argv[i], "--dump-events") == 0) {
			dump_events = true;
		} else if (strcmp(argv[i], "--dump-all") == 0) {
			dump_parsed = true;
			dump_linearized = true;
			dump_accesses = true;
			dump_annotations = true;
			dump_events = true;
		} else if (strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			exit(EXIT_SUCCESS);
		} else {
			argv[dst++] = argv[i];
		}
	}
	argv[dst] = NULL;

	return (dst);
}

static void
show_accesses(struct entrypoint *ep)
{
	struct basic_block *bb;
	char function[128];

	(void) snprintf(function, sizeof (function), "%s",
	    ep->name->ident != NULL ? show_ident(ep->name->ident) :
	    "<anonymous>");
	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;

		FOR_EACH_PTR(bb->insns, insn) {
			struct expression *expr;
			const char *operation;
			struct position pos;

			if (insn->opcode == OP_LOAD)
				operation = "load";
			else if (insn->opcode == OP_STORE)
				operation = "store";
			else
				continue;

			expr = insn->access;
			pos = expr != NULL ? expr->pos : insn->pos;
			(void) printf("%s:%u:%u: %s ",
			    stream_name(pos.stream), pos.line, pos.pos,
			    operation);
			locklint_show_access(stdout, expr);
			(void) printf(" offset=%u function=%s\n",
			    insn->offset, function);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

static void
process_symbols(struct symbol_list *symbols)
{
	struct symbol *sym;

	FOR_EACH_PTR(symbols, sym) {
		struct entrypoint *ep;

		expand_symbol(sym);
		if (dump_parsed)
			show_symbol(sym);

		if (!dump_linearized && !dump_accesses && !dump_events &&
		    !check_locks)
			continue;

		ep = linearize_symbol(sym);
		if (ep == NULL)
			continue;
		if (check_locks)
			locklint_check_add(ep);
		if (dump_linearized)
			show_entry(ep);
		if (dump_accesses)
			show_accesses(ep);
		if (dump_events)
			locklint_show_events(ep);
	} END_FOR_EACH_PTR(sym);
}

int
main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	struct symbol_list *symbols;
	char *file;

	argc = options(argc, argv);
	if (argc == 1) {
		usage(stderr);
		return (EXIT_FAILURE);
	}

	if (dump_annotations || dump_events || check_locks)
		locklint_annotations_enable();
	do_output = 0;
	process_symbols(sparse_initialize(argc, argv, &filelist));
	FOR_EACH_PTR(filelist, file) {
		symbols = sparse(file);
		if (dump_annotations || dump_events || check_locks)
			locklint_resolve_annotations();
		process_symbols(symbols);
	} END_FOR_EACH_PTR(file);
	if (check_locks)
		locklint_check_all();
	if (dump_annotations)
		locklint_show_annotations(stdout);

	return (has_error ? EXIT_FAILURE : EXIT_SUCCESS);
}
