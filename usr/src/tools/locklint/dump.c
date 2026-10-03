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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib.h"
#include "linearize.h"
#include "symbol.h"

#include "dump.h"

struct dump_output {
	const char *name;
	const char *path;
	FILE *stream;
	bool enabled;
	bool header_written;
	bool interleaved;
	bool owned;
	bool spooled;
};

static struct dump_output dump_outputs[DUMP_COUNT] = {
	[DUMP_PARSED] = {
		.name = "dump-parsed",
		/* Spool stdout to avoid interleaving per-function dumps. */
		.interleaved = true
	},
	[DUMP_LINEARIZED] = {
		.name = "dump-linearized",
		/* Spool stdout to avoid interleaving per-function dumps. */
		.interleaved = true
	},
	[DUMP_ACCESSES] = {
		.name = "dump-accesses",
		/* Spool stdout to avoid interleaving per-function dumps. */
		.interleaved = true
	},
	[DUMP_ANNOTATIONS] = {
		.name = "dump-annotations",
		/* Spool stdout to avoid interleaving per-function dumps. */
		.interleaved = true
	},
	[DUMP_EVENTS] = {
		.name = "dump-events",
		/* Spool stdout to avoid interleaving per-function dumps. */
		.interleaved = true
	},
	[DUMP_CALLGRAPH] = { .name = "dump-callgraph" },
	[DUMP_CONTEXTS] = { .name = "dump-contexts" },
	[DUMP_PROTECTION_STATES] = { .name = "dump-protection-states" },
	[DUMP_STATISTICS] = { .name = "dump-statistics" },
	[DUMP_TYPES] = { .name = "dump-types" }
};

static FILE *parse_stream;
static FILE *linearize_stream;

bool
dump_option(const char *argument, enum dump_kind kind)
{
	struct dump_output *output = &dump_outputs[kind];
	size_t length = strlen(output->name);
	const char *value;

	if (strncmp(argument, "--", 2) != 0)
		return (false);
	value = argument + 2;
	if (strcmp(value, output->name) == 0) {
		output->enabled = true;
		return (true);
	}
	if (strncmp(value, output->name, length) != 0 ||
	    value[length] != '=')
		return (false);
	if (value[length + 1] == '\0')
		die("%s requires a non-empty pathname", output->name);
	output->enabled = true;
	output->path = value + length + 1;
	return (true);
}

void
dump_enable_all(void)
{
	size_t kind;

	for (kind = 0; kind < DUMP_COUNT; kind++)
		dump_outputs[kind].enabled = true;
}

bool
dump_is_enabled(enum dump_kind kind)
{
	return (dump_outputs[kind].enabled);
}

/*
 * Create an anonymous on-disk stream for a dump that must be collected before
 * publication.  Immediate unlinking makes every process exit path sufficient
 * cleanup, including signals that cannot be caught.
 */
static FILE *
dump_spool_open(const char *name)
{
	const char *directory = getenv("TMPDIR");
	FILE *stream;
	char *path;
	size_t length;
	int fd;

	if (directory == NULL || directory[0] == '\0')
		directory = "/tmp";
	length = strlen(directory) + strlen(name) + sizeof ("/.XXXXXX");
	path = malloc(length);
	if (path == NULL)
		die("out of memory creating %s temporary output", name);
	(void) snprintf(path, length, "%s/%s.XXXXXX", directory, name);
	fd = mkstemp(path);
	if (fd < 0)
		die("cannot create temporary output for %s", name);
	if (unlink(path) != 0) {
		(void) close(fd);
		die("cannot unlink temporary output for %s", name);
	}
	free(path);
	stream = fdopen(fd, "w+");
	if (stream == NULL) {
		(void) close(fd);
		die("cannot open temporary output for %s", name);
	}
	return (stream);
}

FILE *
dump_stream(enum dump_kind kind)
{
	struct dump_output *output = &dump_outputs[kind];

	if (!output->header_written) {
		(void) fprintf(output->stream, "#### %s ####\n",
		    output->name);
		output->header_written = true;
	}
	return (output->stream);
}

static void
dump_output_prepare(enum dump_kind kind)
{
	struct dump_output *output = &dump_outputs[kind];

	if (output->path != NULL) {
		output->stream = fopen(output->path, "w");
		if (output->stream == NULL)
			die("cannot open %s output '%s'", output->name,
			    output->path);
		output->owned = true;
	} else if (output->interleaved) {
		output->stream = dump_spool_open(output->name);
		output->owned = true;
		output->spooled = true;
	} else {
		output->stream = stdout;
	}
	if (output->interleaved)
		(void) dump_stream(kind);
}

void
dump_outputs_prepare(void)
{
	size_t kind;
	size_t other;

	for (kind = 0; kind < DUMP_COUNT; kind++) {
		if (dump_outputs[kind].path == NULL)
			continue;
		for (other = kind + 1; other < DUMP_COUNT; other++) {
			if (dump_outputs[other].path == NULL)
				continue;
			if (strcmp(dump_outputs[kind].path,
			    dump_outputs[other].path) == 0) {
				die("%s and %s cannot share output pathname '%s'",
				    dump_outputs[kind].name,
				    dump_outputs[other].name,
				    dump_outputs[kind].path);
			}
		}
	}

	for (kind = 0; kind < DUMP_COUNT; kind++) {
		if (dump_outputs[kind].enabled)
			dump_output_prepare(kind);
	}
}

FILE *
dump_output(enum dump_kind kind)
{
	return (dump_outputs[kind].stream);
}

static void
dump_output_publish(struct dump_output *output)
{
	char buffer[8192];
	size_t count;

	if (!output->spooled)
		return;
	if (fflush(output->stream) != 0 ||
	    fseek(output->stream, 0, SEEK_SET) != 0)
		die("cannot rewind temporary %s output", output->name);
	while ((count = fread(buffer, 1, sizeof (buffer),
	    output->stream)) != 0) {
		if (fwrite(buffer, 1, count, stdout) != count)
			die("cannot publish %s output", output->name);
	}
	if (ferror(output->stream))
		die("cannot read temporary %s output", output->name);
}

static void
dump_output_close(struct dump_output *output)
{
	if (!output->owned)
		return;
	if (fclose(output->stream) != 0)
		die("cannot close %s output", output->name);
	output->stream = NULL;
}

void
dump_outputs_finish(void)
{
	size_t kind;

	for (kind = 0; kind < DUMP_COUNT; kind++)
		dump_output_publish(&dump_outputs[kind]);
	for (kind = 0; kind < DUMP_COUNT; kind++)
		dump_output_close(&dump_outputs[kind]);
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
