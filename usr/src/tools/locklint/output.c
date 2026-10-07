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
 * Manage output selection, destinations, conflict detection, stdout
 * spooling, publication, and cleanup independently of output semantics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib.h"

#include "output.h"

static struct locklint_output *outputs;
static struct locklint_output **outputs_tail = &outputs;

void
locklint_output_register(struct locklint_output *output)
{
	if (output->registered)
		die("output '%s' registered more than once", output->name);
	output->registered = true;
	*outputs_tail = output;
	outputs_tail = &output->next;
}

bool
locklint_output_option(const char *argument, struct locklint_output *output)
{
	size_t length = strlen(output->name);
	const char *value;

	if (strncmp(argument, "--", 2) != 0)
		return (false);
	value = argument + 2;
	if (strcmp(value, output->name) == 0) {
		locklint_output_enable(output);
		return (true);
	}
	if (strncmp(value, output->name, length) != 0 ||
	    value[length] != '=')
		return (false);
	if (value[length + 1] == '\0')
		die("%s requires a non-empty pathname", output->name);
	locklint_output_enable(output);
	output->path = value + length + 1;
	return (true);
}

void
locklint_output_enable(struct locklint_output *output)
{
	output->selected = true;
}

bool
locklint_output_is_enabled(const struct locklint_output *output)
{
	return (output->selected);
}

/*
 * Create an anonymous on-disk stream for output collected before publication.
 * Immediate unlinking makes every process exit path sufficient cleanup,
 * including signals that cannot be caught.
 */
static FILE *
output_spool_open(const char *name)
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
locklint_output_stream(struct locklint_output *output)
{
	return (output->stream);
}

FILE *
locklint_output_section_stream(struct locklint_output *output)
{
	if (!output->header_written) {
		(void) fprintf(output->stream, "#### %s ####\n", output->name);
		output->header_written = true;
	}
	return (output->stream);
}

static void
output_prepare(struct locklint_output *output)
{
	if (output->path != NULL) {
		output->stream = fopen(output->path, "w");
		if (output->stream == NULL)
			die("cannot open %s output '%s'", output->name,
			    output->path);
		output->owned = true;
	} else if (output->interleaved) {
		output->stream = output_spool_open(output->name);
		output->owned = true;
		output->spooled = true;
	} else {
		output->stream = stdout;
	}
	if (output->interleaved)
		(void) locklint_output_section_stream(output);
}

void
locklint_outputs_prepare(void)
{
	struct locklint_output *output;
	struct locklint_output *other;

	for (output = outputs; output != NULL; output = output->next) {
		if (output->path == NULL)
			continue;
		for (other = output->next; other != NULL; other = other->next) {
			if (other->path == NULL)
				continue;
			if (strcmp(output->path, other->path) == 0) {
				die("%s and %s cannot share output pathname '%s'",
				    output->name, other->name, output->path);
			}
		}
	}

	for (output = outputs; output != NULL; output = output->next) {
		if (output->selected)
			output_prepare(output);
	}
}

static void
output_publish(struct locklint_output *output)
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
output_close(struct locklint_output *output)
{
	if (!output->owned)
		return;
	if (fclose(output->stream) != 0)
		die("cannot close %s output", output->name);
	output->stream = NULL;
}

void
locklint_outputs_finish(void)
{
	struct locklint_output *output;

	for (output = outputs; output != NULL; output = output->next)
		output_publish(output);
	for (output = outputs; output != NULL; output = output->next)
		output_close(output);
}
