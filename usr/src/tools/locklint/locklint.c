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
#include "analysis.h"
#include "annotations.h"
#include "assertions.h"
#include "callgraph.h"
#include "command_parse.h"
#include "diagnostics.h"
#include "dump.h"
#include "events.h"
#include "identity.h"
#include "lock_identity.h"
#include "lock_order.h"
#include "parse.h"
#include "scope.h"
#include "statistics.h"
#include "symbol.h"
#include "sync_api.h"
#include "timing.h"
#include "type.h"

static bool check_locks = true;
static bool compat_osll;
static bool parser_warnings;
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
	    "[--root-discovery=auto|all-exported|none] "
	    "[--no-check] "
	    "[--parser-warnings] "
	    "[--dump-parsed] [--dump-linearized] "
	    "[--dump-accesses] [--dump-annotations] [--dump-events] "
	    "[--dump-callgraph] [--dump-contexts] "
	    "[--dump-protection-states] [--dump-statistics] [--dump-types] "
	    "(each dump option may use =pathname) "
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
		} else if (strcmp(argv[i], "--no-check") == 0) {
			check_locks = false;
		} else if (strcmp(argv[i], "--parser-warnings") == 0) {
			parser_warnings = true;
		} else if (dump_option(argv[i], DUMP_PARSED)) {
		} else if (dump_option(argv[i], DUMP_LINEARIZED)) {
		} else if (dump_option(argv[i], DUMP_ACCESSES)) {
		} else if (dump_option(argv[i], DUMP_ANNOTATIONS)) {
		} else if (dump_option(argv[i], DUMP_EVENTS)) {
		} else if (dump_option(argv[i], DUMP_CALLGRAPH)) {
		} else if (dump_option(argv[i], DUMP_CONTEXTS)) {
		} else if (dump_option(argv[i], DUMP_PROTECTION_STATES)) {
		} else if (dump_option(argv[i], DUMP_STATISTICS)) {
		} else if (dump_option(argv[i], DUMP_TYPES)) {
		} else if (strcmp(argv[i], "--times") == 0) {
			show_times = true;
		} else if (strcmp(argv[i], "--dump-all") == 0) {
			dump_enable_all();
		} else if (strcmp(argv[i], "--compat=osll") == 0) {
			compat_osll = true;
		} else if (strncmp(argv[i], "--compat=", 9) == 0) {
			die("unknown compatibility mode '%s'", argv[i] + 9);
		} else if (strcmp(argv[i],
		    "--root-discovery=auto") == 0) {
			callgraph_set_root_discovery(
			    CALLGRAPH_ROOT_DISCOVERY_AUTO);
		} else if (strcmp(argv[i],
		    "--root-discovery=all-exported") == 0) {
			callgraph_set_root_discovery(
			    CALLGRAPH_ROOT_DISCOVERY_ALL_EXPORTED);
		} else if (strcmp(argv[i],
		    "--root-discovery=none") == 0) {
			callgraph_set_root_discovery(
			    CALLGRAPH_ROOT_DISCOVERY_NONE);
		} else if (strcmp(argv[i], "--root-discovery") == 0) {
			die("--root-discovery requires a mode");
		} else if (strncmp(argv[i], "--root-discovery=", 17) == 0) {
			die("unknown root discovery mode '%s'", argv[i] + 17);
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
show_accesses(FILE *stream, struct entrypoint *ep)
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
			(void) fprintf(stream, "%s:%u:%u: %s ",
			    stream_name(pos.stream), pos.line, pos.pos,
			    operation);
			locklint_show_access(stream, expr);
			(void) fprintf(stream, " offset=%u function=%s\n",
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
		if (dump_is_enabled(DUMP_PARSED))
			locklint_show_parsed(dump_output(DUMP_PARSED), sym);

		if (!dump_is_enabled(DUMP_LINEARIZED) &&
		    !dump_is_enabled(DUMP_ACCESSES) &&
		    !dump_is_enabled(DUMP_ANNOTATIONS) &&
		    !dump_is_enabled(DUMP_EVENTS) &&
		    !dump_is_enabled(DUMP_CALLGRAPH) &&
		    !dump_is_enabled(DUMP_CONTEXTS) &&
		    !dump_is_enabled(DUMP_PROTECTION_STATES) && !check_locks)
			continue;

		ep = linearize_symbol(sym);
		if (ep == NULL)
			continue;
		if (check_locks || dump_is_enabled(DUMP_CALLGRAPH) ||
		    dump_is_enabled(DUMP_CONTEXTS) ||
		    dump_is_enabled(DUMP_PROTECTION_STATES))
			callgraph_add(tu, ep);
		if (dump_is_enabled(DUMP_LINEARIZED))
			locklint_show_linearized(dump_output(DUMP_LINEARIZED), ep);
		if (dump_is_enabled(DUMP_ACCESSES))
			show_accesses(dump_output(DUMP_ACCESSES), ep);
		if (dump_is_enabled(DUMP_ANNOTATIONS) || check_locks ||
		    dump_is_enabled(DUMP_CONTEXTS) ||
		    dump_is_enabled(DUMP_PROTECTION_STATES))
			locklint_process_function_annotations(
			    dump_is_enabled(DUMP_ANNOTATIONS) ?
			    dump_output(DUMP_ANNOTATIONS) : NULL, tu, ep);
		if (dump_is_enabled(DUMP_EVENTS))
			locklint_show_events(dump_output(DUMP_EVENTS), tu, ep);
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

/*
 * Resolve the whole-program callgraph, run the requested caller-context
 * analysis and reports, then release analysis-owned state.
 */
static void
run_analysis(void)
{
	struct lock_identity_collection lock_identities;
	FILE *callgraph_stream = dump_is_enabled(DUMP_CALLGRAPH) ?
	    dump_output(DUMP_CALLGRAPH) : NULL;
	FILE *context_stream = dump_is_enabled(DUMP_CONTEXTS) ?
	    dump_output(DUMP_CONTEXTS) : NULL;
	FILE *protection_state_stream =
	    dump_is_enabled(DUMP_PROTECTION_STATES) ?
	    dump_output(DUMP_PROTECTION_STATES) : NULL;

	timing_begin(TIMING_ANALYSIS_SETUP);
	callgraph_resolve();
	lock_identity_collection_create(&lock_identities);
	if (check_locks) {
		locklint_order_build();
		locklint_order_report_declared_cycles();
		callgraph_report_unanalyzed_callbacks();
	}
	if (callgraph_stream != NULL) {
		(void) fprintf(callgraph_stream, "#### dump-callgraph ####\n");
		callgraph_dump(callgraph_stream);
	}
	timing_end(TIMING_ANALYSIS_SETUP);
	if (check_locks || context_stream != NULL ||
	    protection_state_stream != NULL) {
		analysis_run(&lock_identities, check_locks, context_stream,
		    protection_state_stream);
	}
	timing_begin(TIMING_DIAG_OBSERVED_ORDER);
	if (check_locks)
		locklint_order_report_observed_cycles();
	timing_end(TIMING_DIAG_OBSERVED_ORDER);
	timing_begin(TIMING_ANALYSIS_CLEANUP);
	if (check_locks)
		locklint_order_cleanup();
	callgraph_cleanup();
	lock_identity_collection_free(&lock_identities);
	timing_end(TIMING_ANALYSIS_CLEANUP);
}

int
main(int argc, char **argv)
{
	char *program_name;
	struct string_list *filelist = NULL;
	struct symbol_list *symbols;
	struct translation_unit *tu;
	char *file;
	int error;

	timing_start();
	/*
	 * Sparse also uses argv[0] as its diagnostic prefix, so shorten it
	 * before either diagnostic system is initialized.
	 */
	program_name = strrchr(argv[0], '/');
	if (program_name == NULL)
		program_name = argv[0];
	else
		program_name++;
	if (*program_name == '\0')
		program_name = "locklint";
	argv[0] = program_name;
	diagnostics_init(program_name);
	error = sync_api_init();
	if (error != 0)
		die("cannot initialize synchronization API: %s",
		    strerror(error));
	argc = options(argc, argv);
	if (argc == 1) {
		usage(stderr);
		sync_api_fini();
		return (EXIT_FAILURE);
	}
	dump_outputs_prepare();
	if (show_times)
		timing_enable();

	type_registry_create();
	preprocessor_compatibility_enable();
	if (dump_is_enabled(DUMP_ANNOTATIONS) ||
	    dump_is_enabled(DUMP_EVENTS) || check_locks ||
	    dump_is_enabled(DUMP_CONTEXTS) ||
	    dump_is_enabled(DUMP_PROTECTION_STATES))
		locklint_annotations_enable();
	if (check_locks || dump_is_enabled(DUMP_CONTEXTS) ||
	    dump_is_enabled(DUMP_PROTECTION_STATES))
		locklint_assertions_enable();
	do_output = 0;
	/*
	 * Illumos uses signed one-bit fields as booleans in established
	 * interfaces.  Accept them rather than letting Sparse's portability
	 * diagnostic prevent lock analysis.
	 */
	Wone_bit_signed_bitfield = 0;
	/*
	 * Parser warnings are troubleshooting output rather than locklint
	 * findings.  An explicit Sparse -fmax-warnings option may still replace
	 * this default while sparse_initialize() processes its arguments.
	 */
	fmax_warnings = parser_warnings ? ~0U : 0;
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
	if (dump_is_enabled(DUMP_ANNOTATIONS) ||
	    dump_is_enabled(DUMP_EVENTS) || check_locks ||
	    dump_is_enabled(DUMP_CONTEXTS) ||
	    dump_is_enabled(DUMP_PROTECTION_STATES))
		locklint_resolve_annotations(symbols);
	if (check_locks || dump_is_enabled(DUMP_CALLGRAPH) ||
	    dump_is_enabled(DUMP_CONTEXTS) ||
	    dump_is_enabled(DUMP_PROTECTION_STATES))
		callgraph_record_pointer_evidence(tu, symbols,
		    dump_is_enabled(DUMP_CALLGRAPH));
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
		if (dump_is_enabled(DUMP_ANNOTATIONS) ||
		    dump_is_enabled(DUMP_EVENTS) || check_locks ||
		    dump_is_enabled(DUMP_CONTEXTS) ||
		    dump_is_enabled(DUMP_PROTECTION_STATES))
			locklint_resolve_annotations(symbols);
		if (check_locks || dump_is_enabled(DUMP_CALLGRAPH) ||
		    dump_is_enabled(DUMP_CONTEXTS) ||
		    dump_is_enabled(DUMP_PROTECTION_STATES))
			callgraph_record_pointer_evidence(tu, symbols,
			    dump_is_enabled(DUMP_CALLGRAPH));
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
		sync_api_fini();
		return (EXIT_FAILURE);
	}
	locklint_apply_contract_annotations();
	timing_begin(TIMING_COMMANDS);
	if (!parse_command_files()) {
		locklint_access_cleanup();
		sync_api_fini();
		return (EXIT_FAILURE);
	}
	timing_end(TIMING_COMMANDS);
	if (check_locks || dump_is_enabled(DUMP_CALLGRAPH) ||
	    dump_is_enabled(DUMP_CONTEXTS) ||
	    dump_is_enabled(DUMP_PROTECTION_STATES)) {
		run_analysis();
	}
	timing_begin(TIMING_FINAL_OUTPUT);
	if (dump_is_enabled(DUMP_TYPES))
		type_registry_show(dump_stream(DUMP_TYPES));
	if (dump_is_enabled(DUMP_STATISTICS))
		statistics_show(dump_stream(DUMP_STATISTICS));
	if (dump_is_enabled(DUMP_ANNOTATIONS))
		locklint_show_annotations(dump_output(DUMP_ANNOTATIONS));
	dump_outputs_finish();
	locklint_access_cleanup();
	sync_api_fini();
	(void) fflush(stdout);
	timing_end(TIMING_FINAL_OUTPUT);
	timing_report(stderr);

	return (has_error ? EXIT_FAILURE : EXIT_SUCCESS);
}
