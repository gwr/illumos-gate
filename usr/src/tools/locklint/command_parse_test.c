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
 * Exercise command-file parsing independently of locklint semantics.  Each
 * command handler checks the source line and words passed by the parser.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "command_parse.h"

struct expected_command {
	const char *command;
	unsigned long line;
	int argc;
	const char *argv[3];
};

static const struct expected_command expected_commands[] = {
	{ "assert", 4, 2, { "a1", "a2" } },
	{ "declare", 5, 2, { "d1", "d2" } },
	{ "declare", 6, 3, { "continued1", "continued2", "continued3" } },
	{ "ignore", 9, 2, { "i1", "i2" } },
	{ "merge-instances", 10, 2, { "m1", "m2" } },
	{ "lock-role-protects-data", 11, 3,
	    { "role", "data1", "data2" } }
};

static size_t command_index;

static int
check_command(const char *command, int argc, char **argv)
{
	const struct expected_command *expected;
	int i;

	if (command_index >= sizeof (expected_commands) /
	    sizeof (expected_commands[0])) {
		(void) fprintf(stderr, "unexpected command '%s'\n", command);
		return (-1);
	}
	expected = &expected_commands[command_index];
	if (strcmp(command, expected->command) != 0 ||
	    command_parse_line() != expected->line ||
	    argc != expected->argc) {
		(void) fprintf(stderr,
		    "command %zu: expected %s at line %lu with %d arguments; "
		    "got %s at line %lu with %d arguments\n",
		    command_index + 1, expected->command, expected->line,
		    expected->argc, command, command_parse_line(), argc);
		return (-1);
	}
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], expected->argv[i]) != 0) {
			(void) fprintf(stderr,
			    "command %zu argument %d: expected '%s'; got '%s'\n",
			    command_index + 1, i + 1, expected->argv[i],
			    argv[i]);
			return (-1);
		}
	}
	command_index++;
	return (0);
}

int
cmd_assert(int argc, char **argv)
{
	return (check_command("assert", argc, argv));
}

int
cmd_declare(int argc, char **argv)
{
	return (check_command("declare", argc, argv));
}

int
cmd_ignore(int argc, char **argv)
{
	return (check_command("ignore", argc, argv));
}

int
cmd_lock_role_protects_data(int argc, char **argv)
{
	return (check_command("lock-role-protects-data", argc, argv));
}

int
cmd_merge_instances(int argc, char **argv)
{
	return (check_command("merge-instances", argc, argv));
}

int
main(int argc, char **argv)
{
	int result;

	if (argc != 2) {
		(void) fprintf(stderr, "usage: command_parse_test file\n");
		return (EXIT_FAILURE);
	}
	result = command_parse_file(argv[1]);
	if (result == 0 && command_index != sizeof (expected_commands) /
	    sizeof (expected_commands[0])) {
		(void) fprintf(stderr, "expected %zu commands; got %zu\n",
		    sizeof (expected_commands) / sizeof (expected_commands[0]),
		    command_index);
		result = -1;
	}
	return (result == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
