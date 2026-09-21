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
 * Provide the locklint command-line interface and drive Sparse parsing and
 * lock analysis.  This program builds upon the features of the "sparse"
 * library over in ../smatch/src/ (See Documentation/sparse-README.txt)
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
#include "assertions.h"
#include "callgraph.h"
#include "check.h"
#include "command_parse.h"
#include "events.h"
#include "identity.h"
#include "parse.h"
#include "scope.h"
#include "statistics.h"
#include "symbol.h"
#include "timing.h"
#include "type.h"

static bool dump_parsed;
static bool dump_linearized;
static bool dump_accesses;
static bool dump_annotations;
static bool dump_events;
static bool dump_callgraph;
static bool dump_contexts;
static bool dump_statistics;
static bool dump_types;
static bool check_locks;
static bool compat_osll;
static bool show_times;

struct command_file {
	const char *path;
	struct command_file *next;
};

static struct command_file *command_files;
static struct command_file **command_files_tail = &command_files;

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
	    "usage: locklint [--cf command-file] [--compat=osll] "
	    "[--check-locks] [--dump-parsed] [--dump-linearized] "
	    "[--dump-accesses] [--dump-annotations] [--dump-events] "
	    "[--dump-callgraph] [--dump-contexts] [--dump-statistics] "
	    "[--dump-types] "
	    "[--times] "
	    "[compiler-options] file.c ...\n");
}

/*
 * Preserve command-file option order while removing the option and pathname
 * from the arguments that Sparse will process.
 */
static void
add_command_file(const char *path)
{
	struct command_file *file;

	file = calloc(1, sizeof (*file));
	if (file == NULL)
		die("out of memory recording command file");
	file->path = path;
	*command_files_tail = file;
	command_files_tail = &file->next;
}

static int
options(int argc, char **argv)
{
	int dst = 1;
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--cf") == 0) {
			if (++i == argc)
				die("--cf requires a command file");
			add_command_file(argv[i]);
		} else if (strcmp(argv[i], "--check-locks") == 0) {
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
		} else if (strcmp(argv[i], "--dump-callgraph") == 0) {
			dump_callgraph = true;
		} else if (strcmp(argv[i], "--dump-contexts") == 0) {
			dump_contexts = true;
		} else if (strcmp(argv[i], "--dump-statistics") == 0) {
			dump_statistics = true;
		} else if (strcmp(argv[i], "--dump-types") == 0) {
			dump_types = true;
		} else if (strcmp(argv[i], "--times") == 0) {
			show_times = true;
		} else if (strcmp(argv[i], "--dump-all") == 0) {
			dump_parsed = true;
			dump_linearized = true;
			dump_accesses = true;
			dump_annotations = true;
			dump_events = true;
			dump_callgraph = true;
			dump_contexts = true;
			dump_statistics = true;
			dump_types = true;
		} else if (strcmp(argv[i], "--compat=osll") == 0) {
			compat_osll = true;
		} else if (strncmp(argv[i], "--compat=", 9) == 0) {
			die("unknown compatibility mode '%s'", argv[i] + 9);
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

/*
 * Parse all command files only after every C translation unit has registered
 * its types and symbols, but before whole-program checking begins.
 */
static bool
parse_command_files(void)
{
	struct command_file *file;

	for (file = command_files; file != NULL; file = file->next) {
		if (command_parse_file(file->path) != 0)
			return (false);
	}
	return (true);
}

/*
 * Identify the new analyzer in every mode.  OSLL compatibility additionally
 * selects historical source paths guarded for the old analyzer.
 */
static void
preprocessor_compatibility_enable(void)
{
	add_pre_buffer("#define __locklint__ 1\n");
	if (compat_osll)
		add_pre_buffer("#define __lock_lint 1\n");
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
process_symbols(struct translation_unit *tu, struct symbol_list *symbols)
{
	struct symbol *sym;

	FOR_EACH_PTR(symbols, sym) {
		struct entrypoint *ep;

		expand_symbol(sym);
		if (dump_parsed)
			show_symbol(sym);

		if (!dump_linearized && !dump_accesses && !dump_annotations &&
		    !dump_events && !dump_callgraph && !dump_contexts &&
		    !check_locks)
			continue;

		ep = linearize_symbol(sym);
		if (ep == NULL)
			continue;
		if (check_locks || dump_callgraph || dump_contexts)
			callgraph_add(tu, ep);
		if (dump_linearized)
			show_entry(ep);
		if (dump_accesses)
			show_accesses(ep);
		if (dump_annotations || check_locks || dump_contexts)
			locklint_process_function_annotations(
			    dump_annotations ? stdout : NULL, tu, ep);
		if (dump_events)
			locklint_show_events(tu, ep);
	} END_FOR_EACH_PTR(sym);
}

static bool
multiple_inputs(struct string_list *filelist)
{
	unsigned int count = 0;
	char *file;

	FOR_EACH_PTR(filelist, file) {
		(void) file;
		count++;
	} END_FOR_EACH_PTR(file);
	return (count > 1);
}

static bool
initial_internal_declarations(struct symbol_list *symbols)
{
	struct symbol *symbol;
	bool found = false;

	FOR_EACH_PTR(symbols, symbol) {
		unsigned long modifiers = symbol->ctype.modifiers;

		if ((modifiers & (MOD_TOPLEVEL | MOD_STATIC)) ==
		    (MOD_TOPLEVEL | MOD_STATIC))
			found = true;
	} END_FOR_EACH_PTR(symbol);
	return (found);
}

/*
 * Preserve declarations needed after Sparse leaves this translation unit's
 * namespaces.  The returned symbol list does not necessarily include extern
 * declarations contributed by headers, so include the active outer scopes.
 */
static void
register_translation_unit_declarations(struct translation_unit *tu,
    struct symbol_list *symbols)
{
	timing_begin(TIMING_INPUT_IDENTITIES);
	locklint_translation_unit_register(tu, symbols);
	locklint_translation_unit_register(tu, file_scope->symbols);
	locklint_translation_unit_register(tu, global_scope->symbols);
	timing_end(TIMING_INPUT_IDENTITIES);
	timing_begin(TIMING_INPUT_TYPES);
	type_symbols_register(symbols);
	type_symbols_register(file_scope->symbols);
	type_symbols_register(global_scope->symbols);
	timing_end(TIMING_INPUT_TYPES);
}

/*
 * Parse and evaluate one input without releasing its preprocessing tokens.
 * Sparse's process-wide macro table retains token positions, so all input
 * token arenas must remain live until the last translation unit is parsed.
 */
static struct symbol_list *
locklint_sparse(char *filename)
{
	struct symbol_list *symbols;

	symbols = sparse_keep_tokens(filename);
	if (has_error & ERROR_CURR_PHASE)
		has_error = ERROR_PREV_PHASE;
	evaluate_symbol_list(symbols);
	return (symbols);
}

int
main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	struct symbol_list *symbols;
	struct translation_unit *tu;
	char *file;

	timing_start();
	argc = options(argc, argv);
	if (argc == 1) {
		usage(stderr);
		return (EXIT_FAILURE);
	}
	if (show_times)
		timing_enable();

	type_registry_create();
	preprocessor_compatibility_enable();
	if (dump_annotations || dump_events || check_locks || dump_contexts)
		locklint_annotations_enable();
	if (check_locks || dump_contexts)
		locklint_assertions_enable();
	do_output = 0;
	/*
	 * Illumos uses signed one-bit fields as booleans in established
	 * interfaces.  Accept them rather than letting Sparse's portability
	 * diagnostic prevent lock analysis.
	 */
	Wone_bit_signed_bitfield = 0;
	/*
	 * Sparse's default warning cap is useful for broad checker runs.
	 * Locklint diagnostics are a correctness result, so they must be
	 * complete by default.  sparse_initialize() may still replace this
	 * value when the user explicitly supplies -fmax-warnings.
	 */
	fmax_warnings = ~0U;
	/*
	 * Sparse parses predefined and command-line-included source before the
	 * explicit inputs.  Give records captured there stable provenance, and
	 * resolve their names while that initialization namespace is current.
	 */
	tu = locklint_translation_unit_begin("<Sparse initialization>");
	symbols = sparse_initialize(argc, argv, &filelist);
	timing_end(TIMING_INITIALIZE);
	/*
	 * Sparse parses a forced include once during initialization rather than
	 * once per explicit input.  Sharing its internal-linkage declarations
	 * would conflate distinct C objects or functions, so reject that case
	 * until the frontend can provide a separate instance for each input.
	 */
	if (multiple_inputs(filelist) &&
	    initial_internal_declarations(symbols))
		die("multiple inputs with initialization-time internal "
		    "declarations are not supported");
	register_translation_unit_declarations(tu, symbols);
	timing_begin(TIMING_INPUT_EVIDENCE);
	if (dump_annotations || dump_events || check_locks || dump_contexts)
		locklint_resolve_annotations(symbols);
	if (check_locks || dump_callgraph || dump_contexts)
		callgraph_record_pointer_evidence(tu, symbols,
		    dump_callgraph);
	timing_end(TIMING_INPUT_EVIDENCE);
	timing_begin(TIMING_INPUT_SYMBOLS);
	process_symbols(tu, symbols);
	timing_end(TIMING_INPUT_SYMBOLS);
	FOR_EACH_PTR(filelist, file) {
		/*
		 * Hooks run while Sparse parses the file, so establish provenance
		 * first.
		 * Register internal objects before resolving captured names.  Name
		 * resolution must finish here: parsing the next input removes this
		 * file scope and may replace the visible declaration chains.
		 */
		timing_begin(TIMING_INPUT_PARSE);
		tu = locklint_translation_unit_begin(file);
		symbols = locklint_sparse(file);
		timing_end(TIMING_INPUT_PARSE);
		register_translation_unit_declarations(tu, symbols);
		timing_begin(TIMING_INPUT_EVIDENCE);
		if (dump_annotations || dump_events || check_locks ||
		    dump_contexts)
			locklint_resolve_annotations(symbols);
		if (check_locks || dump_callgraph || dump_contexts)
			callgraph_record_pointer_evidence(tu, symbols,
			    dump_callgraph);
		timing_end(TIMING_INPUT_EVIDENCE);
		timing_begin(TIMING_INPUT_SYMBOLS);
		process_symbols(tu, symbols);
		timing_end(TIMING_INPUT_SYMBOLS);
	} END_FOR_EACH_PTR(file);
	timing_begin(TIMING_INPUT_CLEANUP);
	clear_token_alloc();
	timing_end(TIMING_INPUT_CLEANUP);
	if (!type_registry_consistent()) {
		type_registry_report_errors();
		locklint_access_cleanup();
		return (EXIT_FAILURE);
	}
	timing_begin(TIMING_COMMANDS);
	if (!parse_command_files()) {
		locklint_access_cleanup();
		return (EXIT_FAILURE);
	}
	timing_end(TIMING_COMMANDS);
	if (check_locks || dump_callgraph || dump_contexts)
		locklint_check_all(check_locks, dump_callgraph, dump_contexts);
	timing_begin(TIMING_FINAL_OUTPUT);
	if (dump_types)
		type_registry_show(stdout);
	if (dump_statistics)
		statistics_show(stdout);
	if (dump_annotations)
		locklint_show_annotations(stdout);
	locklint_access_cleanup();
	(void) fflush(stdout);
	timing_end(TIMING_FINAL_OUTPUT);
	timing_report(stderr);

	return (has_error ? EXIT_FAILURE : EXIT_SUCCESS);
}
