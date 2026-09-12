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
 * command handler prints exactly the words passed to it by the parser.
 */

#include <stdio.h>
#include <stdlib.h>

#include "command_parse.h"

static int
print_command(const char *command, int argc, char **argv)
{
	int i;

	(void) printf("%s:", command);
	for (i = 0; i < argc; i++)
		(void) printf(" %s", argv[i]);
	(void) putchar('\n');
	return (0);
}

int
cmd_assert(int argc, char **argv)
{
	return (print_command("assert", argc, argv));
}

int
cmd_declare(int argc, char **argv)
{
	return (print_command("declare", argc, argv));
}

int
cmd_ignore(int argc, char **argv)
{
	return (print_command("ignore", argc, argv));
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		(void) fprintf(stderr, "usage: command_parse_test file\n");
		return (EXIT_FAILURE);
	}
	return (command_parse_file(argv[1]) == 0 ?
	    EXIT_SUCCESS : EXIT_FAILURE);
}
