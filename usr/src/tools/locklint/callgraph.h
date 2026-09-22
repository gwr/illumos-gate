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

#ifndef CALLGRAPH_H
#define	CALLGRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct entrypoint;
struct function_info;
struct instruction;
struct call_target_set;
struct symbol;
struct symbol_list;
struct translation_unit;

enum callgraph_declare_result {
	CALLGRAPH_DECLARE_OK,
	CALLGRAPH_DECLARE_UNRESOLVED,
	CALLGRAPH_DECLARE_AMBIGUOUS,
	CALLGRAPH_DECLARE_CONFLICT
};

/*
 * The callgraph progresses through four states:
 *
 *   CONSTRUCTING -> RESOLVING -> READY -> CLEANED
 *
 * Functions and pointer evidence may be added only while constructing the
 * graph.  callgraph_resolve() closes construction and performs the deferred
 * whole-program resolution needed to make the graph ready.  Iteration,
 * callee queries, and audit output are valid only while the graph is ready.
 */

/*
 * Add a linearized function to the graph under construction.
 */
void callgraph_add(struct translation_unit *, struct entrypoint *);

/*
 * Record function-address escapes and, when requested, function-pointer load
 * and store activity from one translation unit.  Also retain exact indirect
 * targets found in supported static const aggregate initializers.
 */
void callgraph_record_pointer_evidence(struct translation_unit *,
    struct symbol_list *, bool record_activity);

/*
 * Declare an externally named analysis entry with an exact no-competition
 * entry condition.  The declaration is accepted while the graph is under
 * construction, after all translation units have registered their functions.
 */
enum callgraph_declare_result
callgraph_declare_entry_no_competing_threads(const char *, const char *,
    unsigned long);

/*
 * Declare whether an externally defined function is an entry from outside
 * the analyzed inputs.  False suppresses only the external-linkage automatic
 * root reason; other root evidence remains effective.
 */
enum callgraph_declare_result callgraph_declare_external_entry(const char *,
    bool, const char *, unsigned long);

/*
 * Close construction, resolve function identities and indirect targets,
 * classify roots, compute reachability, and make the graph ready.
 */
void callgraph_resolve(void);

/*
 * Write the callgraph audit to the requested stream.  The graph must be ready.
 */
void callgraph_dump(FILE *);

/*
 * Report function-pointer escapes that cannot be resolved to one retained
 * definition.  Each function identity is reported once at its first escape.
 */
void callgraph_report_unanalyzed_callbacks(void);

/*
 * An iterator is an opaque cursor over the complete function set.  Opening an
 * iterator returns zero on success or an errno value if the graph is not ready
 * or the iterator cannot be allocated; the output pointer is unchanged on
 * failure.  Each successful open must be matched by a close.
 */
struct callgraph_iter;

int callgraph_iter_open(struct callgraph_iter **);
struct function_info *callgraph_iter_next(struct callgraph_iter *);
void callgraph_iter_close(struct callgraph_iter *);

/*
 * Resolve a call instruction in the context of its caller.  This handles
 * direct calls and the supported exact indirect-call forms.  A NULL result
 * means that no unique callee is known.
 */
struct function_info *callgraph_callee(const struct function_info *,
    const struct instruction *);

/*
 * Iterate every statically resolved target of one call.  Target order is
 * stable within one resolved callgraph.
 */
size_t callgraph_target_count(const struct function_info *,
    const struct instruction *);
struct function_info *callgraph_target(const struct function_info *,
    const struct instruction *, size_t);
const struct call_target_set *callgraph_symbol_targets(
    const struct function_info *, const struct symbol *);
size_t callgraph_target_set_count(const struct call_target_set *);
struct function_info *callgraph_target_set_target(
    const struct call_target_set *, size_t);

/*
 * Return true when a direct call cannot be resolved because multiple external
 * definitions have the referenced name.
 */
bool callgraph_ambiguous_callee(const struct function_info *,
    const struct instruction *);

/*
 * Release all callgraph-owned records and transition to the cleaned state.
 * Checker-owned analysis data attached to function records must already have
 * been released, and no iterators may remain open.
 */
void callgraph_cleanup(void);

#endif /* CALLGRAPH_H */
