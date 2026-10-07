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
 * Manage the fixed collection of development dumps and route the printf-based
 * Sparse parsed and linearized renderers to their selected dump streams.
 */

#include <stdarg.h>
#include <stdio.h>

#include "linearize.h"
#include "symbol.h"

#include "dump.h"
#include "output.h"

struct dump_output {
	struct locklint_output output;
};

static struct dump_output dump_outputs[DUMP_COUNT] = {
	[DUMP_PARSED] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-parsed", true)
	},
	[DUMP_LINEARIZED] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-linearized", true)
	},
	[DUMP_ACCESSES] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-accesses", true)
	},
	[DUMP_ANNOTATIONS] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-annotations", true)
	},
	[DUMP_EVENTS] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-events", true)
	},
	[DUMP_CALLGRAPH] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-callgraph", false)
	},
	[DUMP_CONTEXTS] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-contexts", false)
	},
	[DUMP_PROTECTION_STATES] = {
		.output =
		    LOCKLINT_OUTPUT_INITIALIZER("dump-protection-states", false)
	},
	[DUMP_STATISTICS] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-statistics", false)
	},
	[DUMP_TYPES] = {
		.output = LOCKLINT_OUTPUT_INITIALIZER("dump-types", false)
	}
};

static FILE *parse_stream;
static FILE *linearize_stream;

bool
dump_option(const char *argument, enum dump_kind kind)
{
	return (locklint_output_option(argument,
	    &dump_outputs[kind].output));
}

void
dump_outputs_register(void)
{
	size_t kind;

	for (kind = 0; kind < DUMP_COUNT; kind++)
		locklint_output_register(&dump_outputs[kind].output);
}

void
dump_enable_all(void)
{
	size_t kind;

	for (kind = 0; kind < DUMP_COUNT; kind++)
		locklint_output_enable(&dump_outputs[kind].output);
}

bool
dump_is_enabled(enum dump_kind kind)
{
	return (locklint_output_is_enabled(&dump_outputs[kind].output));
}

FILE *
dump_stream(enum dump_kind kind)
{
	return (locklint_output_section_stream(&dump_outputs[kind].output));
}

FILE *
dump_output(enum dump_kind kind)
{
	return (locklint_output_stream(&dump_outputs[kind].output));
}

int
locklint_printf_parse(const char *format, ...)
{
	FILE *stream = parse_stream != NULL ? parse_stream : stdout;
	va_list args;
	int result;

	va_start(args, format);
	result = vfprintf(stream, format, args);
	va_end(args);
	return (result);
}

int
locklint_printf_linearize(const char *format, ...)
{
	FILE *stream = linearize_stream != NULL ? linearize_stream : stdout;
	va_list args;
	int result;

	va_start(args, format);
	result = vfprintf(stream, format, args);
	va_end(args);
	return (result);
}

void
locklint_show_parsed(FILE *stream, struct symbol *symbol)
{
	FILE *saved = parse_stream;

	parse_stream = stream;
	show_symbol(symbol);
	parse_stream = saved;
}

void
locklint_show_linearized(FILE *stream, struct entrypoint *entrypoint)
{
	FILE *saved = linearize_stream;

	linearize_stream = stream;
	show_entry(entrypoint);
	linearize_stream = saved;
}
