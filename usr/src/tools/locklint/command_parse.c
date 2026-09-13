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
 * whitespace-separated words, and the first '#' starts a comment.  Parsing
 * stops at the first error so partially valid input is never analyzed.
 */

#include <errno.h>
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

	return (command_parse_error("unknown command '%s'", command));
}

/*
 * Read and dispatch a complete command file.
 * getline() avoids imposing an arbitrary command length or argument count.
 */
int
command_parse_file(const char *path)
{
	char **argv = NULL;
	char *line = NULL;
	size_t argv_capacity = 0;
	size_t line_capacity = 0;
	FILE *stream;
	int result = 0;

	stream = fopen(path, "r");
	if (stream == NULL) {
		(void) fprintf(stderr, "%s: %s\n", path, strerror(errno));
		return (-1);
	}

	command_file = path;
	for (command_line = 1;
	    getline(&line, &line_capacity, stream) != -1; command_line++) {
		char *command;
		char *comment;
		char *last;
		char *word;
		int argc = 0;

		if ((comment = strchr(line, '#')) != NULL)
			*comment = '\0';
		command = strtok_r(line, " \t\r\n", &last);
		if (command == NULL)
			continue;

		while ((word = strtok_r(NULL, " \t\r\n", &last)) != NULL) {
			if (add_argument(&argv, &argv_capacity, argc, word) != 0) {
				result = -1;
				goto out;
			}
			argc++;
		}
		if (argv != NULL)
			argv[argc] = NULL;
		if (dispatch_command(command, argc, argv) != 0) {
			result = -1;
			goto out;
		}
	}
	if (ferror(stream)) {
		(void) fprintf(stderr, "%s: %s\n", path, strerror(errno));
		result = -1;
	}

out:
	free(argv);
	free(line);
	if (fclose(stream) != 0 && result == 0) {
		(void) fprintf(stderr, "%s: %s\n", path, strerror(errno));
		result = -1;
	}
	command_file = NULL;
	command_line = 0;
	return (result);
}
