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
 * Parse the deliberately small locklint command language.  Commands contain
 * whitespace-separated words, backslash-newline joins physical lines, and the
 * first '#' starts a comment.  Parsing stops at the first error so partially
 * valid input is never analyzed.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "command_parse.h"

static const char *command_file;
static unsigned long command_line;

const char *
command_parse_path(void)
{
	return (command_file);
}

unsigned long
command_parse_line(void)
{
	return (command_line);
}

int
command_parse_error(const char *format, ...)
{
	va_list args;

	(void) fprintf(stderr, "%s:%lu: ", command_file, command_line);
	va_start(args, format);
	(void) vfprintf(stderr, format, args);
	va_end(args);
	(void) fputc('\n', stderr);
	return (-1);
}

/*
 * Add one argument while keeping the vector NULL-terminated for handlers.
 */
static int
add_argument(char ***argvp, size_t *capacityp, int argc, char *argument)
{
	char **new_argv;
	size_t capacity;

	if ((size_t)argc + 1 >= *capacityp) {
		capacity = *capacityp == 0 ? 8 : *capacityp * 2;
		new_argv = realloc(*argvp, capacity * sizeof (**argvp));
		if (new_argv == NULL)
			return (command_parse_error("out of memory"));
		*argvp = new_argv;
		*capacityp = capacity;
	}
	(*argvp)[argc] = argument;
	(*argvp)[argc + 1] = NULL;
	return (0);
}

/*
 * Dispatch one tokenized command.  Subcommand grammar belongs to the
 * individual handler, not to the common file parser.
 */
static int
dispatch_command(const char *command, int argc, char **argv)
{
	if (strcmp(command, "assert") == 0)
		return (cmd_assert(argc, argv));
	if (strcmp(command, "declare") == 0)
		return (cmd_declare(argc, argv));
	if (strcmp(command, "ignore") == 0)
		return (cmd_ignore(argc, argv));
	if (strcmp(command, "merge-instances") == 0)
		return (cmd_merge_instances(argc, argv));

	return (command_parse_error("unknown command '%s'", command));
}

/*
 * Append one physical-line fragment without imposing a logical-line length.
 * Geometric growth keeps repeated continuations linear in the total input.
 */
static int
append_text(char **textp, size_t *capacityp, size_t *lengthp,
    const char *fragment, size_t fragment_length)
{
	char *new_text;
	size_t capacity;
	size_t needed;

	if (fragment_length > SIZE_MAX - *lengthp - 1)
		return (command_parse_error("command is too long"));
	needed = *lengthp + fragment_length + 1;
	if (needed > *capacityp) {
		capacity = *capacityp == 0 ? 128 : *capacityp;
		while (capacity < needed) {
			if (capacity > SIZE_MAX / 2) {
				capacity = needed;
				break;
			}
			capacity *= 2;
		}
		new_text = realloc(*textp, capacity);
		if (new_text == NULL)
			return (command_parse_error("out of memory"));
		*textp = new_text;
		*capacityp = capacity;
	}
	(void) memcpy(*textp + *lengthp, fragment, fragment_length);
	*lengthp += fragment_length;
	(*textp)[*lengthp] = '\0';
	return (0);
}

/*
 * Tokenize and dispatch one complete logical line.
 */
static int
dispatch_line(char *line, char ***argvp, size_t *argv_capacityp)
{
	char *command;
	char *comment;
	char *last;
	char *word;
	int argc = 0;

	if ((comment = strchr(line, '#')) != NULL)
		*comment = '\0';
	command = strtok_r(line, " \t\r\n", &last);
	if (command == NULL)
		return (0);

	while ((word = strtok_r(NULL, " \t\r\n", &last)) != NULL) {
		if (add_argument(argvp, argv_capacityp, argc, word) != 0)
			return (-1);
		argc++;
	}
	if (*argvp != NULL)
		(*argvp)[argc] = NULL;
	return (dispatch_command(command, argc, *argvp));
}

/*
 * Read and dispatch a complete command file.
 * getline() avoids imposing an arbitrary command length or argument count.
 */
int
command_parse_file(const char *path)
{
	char **argv = NULL;
	char *logical = NULL;
	char *physical = NULL;
	size_t argv_capacity = 0;
	size_t logical_capacity = 0;
	size_t logical_length = 0;
	size_t physical_capacity = 0;
	FILE *stream;
	unsigned long physical_line = 0;
	ssize_t physical_length;
	bool command_open = false;
	int result = 0;

	stream = fopen(path, "r");
	if (stream == NULL) {
		(void) fprintf(stderr, "%s: %s\n", path, strerror(errno));
		return (-1);
	}

	command_file = path;
	while ((physical_length = getline(&physical, &physical_capacity,
	    stream)) != -1) {
		bool continued;
		size_t fragment_length = physical_length;

		physical_line++;
		if (!command_open) {
			command_line = physical_line;
			command_open = true;
		}
		continued = fragment_length >= 2 &&
		    physical[fragment_length - 2] == '\\' &&
		    physical[fragment_length - 1] == '\n';
		if (continued)
			fragment_length -= 2;
		if (append_text(&logical, &logical_capacity, &logical_length,
		    physical, fragment_length) != 0) {
			result = -1;
			goto out;
		}
		if (continued)
			continue;

		if (dispatch_line(logical, &argv, &argv_capacity) != 0) {
			result = -1;
			goto out;
		}
		logical_length = 0;
		command_open = false;
	}
	if (ferror(stream)) {
		(void) fprintf(stderr, "%s: %s\n", path, strerror(errno));
		result = -1;
	} else if (command_open &&
	    dispatch_line(logical, &argv, &argv_capacity) != 0) {
		result = -1;
	}

out:
	free(argv);
	free(logical);
	free(physical);
	if (fclose(stream) != 0 && result == 0) {
		(void) fprintf(stderr, "%s: %s\n", path, strerror(errno));
		result = -1;
	}
	command_file = NULL;
	command_line = 0;
	return (result);
}
