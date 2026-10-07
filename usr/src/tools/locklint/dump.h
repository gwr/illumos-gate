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
 * Manage development dump selection, destinations, and publication.
 */

#ifndef DUMP_H
#define	DUMP_H

#include <stdbool.h>
#include <stdio.h>

struct entrypoint;
struct symbol;

enum dump_kind {
	DUMP_PARSED,
	DUMP_LINEARIZED,
	DUMP_ACCESSES,
	DUMP_ANNOTATIONS,
	DUMP_EVENTS,
	DUMP_CALLGRAPH,
	DUMP_CONTEXTS,
	DUMP_PROTECTION_STATES,
	DUMP_STATISTICS,
	DUMP_TYPES,
	DUMP_COUNT
};

/*
 * Option processing selects individual dumps or the complete collection.
 * After option processing, prepare all selected destinations before requesting
 * their streams, then finish them to publish spooled output and close owned
 * files.  dump_output() returns the prepared stream unchanged; dump_stream()
 * emits the dump's header once before returning the stream.
 */
bool dump_option(const char *, enum dump_kind);
void dump_outputs_register(void);
void dump_enable_all(void);
bool dump_is_enabled(enum dump_kind);
FILE *dump_output(enum dump_kind);
FILE *dump_stream(enum dump_kind);

int locklint_printf_parse(const char *, ...);
int locklint_printf_linearize(const char *, ...);
void locklint_show_parsed(FILE *, struct symbol *);
void locklint_show_linearized(FILE *, struct entrypoint *);

#endif /* DUMP_H */
