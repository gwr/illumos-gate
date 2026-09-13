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
 * Implement locklint command semantics.  The command-file interface is wired
 * before individual commands acquire semantics, so recognized commands fail
 * explicitly rather than appearing to configure an analysis.
 */

#include <string.h>

#include "annotations.h"
#include "command_parse.h"

static int
not_implemented(const char *command)
{
	return (command_parse_error("%s command is not implemented", command));
}

int
cmd_assert(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	return (not_implemented("assert"));
}

int
cmd_declare(int argc, char **argv)
{
	enum locklint_command_result result;

	if (argc == 0)
		return (command_parse_error("declare requires a declaration kind"));
	if (strcmp(argv[0], "readable") != 0)
		return (not_implemented("declare"));
	if (argc != 2) {
		return (command_parse_error(
		    "declare readable requires one data name"));
	}
	result = locklint_declare_readable(argv[1], command_parse_path(),
	    command_parse_line());
	switch (result) {
	case LOCKLINT_COMMAND_OK:
		return (0);
	case LOCKLINT_COMMAND_INVALID_NAME:
		return (command_parse_error("invalid data name '%s'", argv[1]));
	case LOCKLINT_COMMAND_UNRESOLVED_NAME:
		return (command_parse_error("unresolved data name '%s'",
		    argv[1]));
	case LOCKLINT_COMMAND_AMBIGUOUS_NAME:
		return (command_parse_error("ambiguous data name '%s'",
		    argv[1]));
	default:
		return (command_parse_error(
		    "internal error resolving data name '%s'", argv[1]));
	}
}

int
cmd_ignore(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	return (not_implemented("ignore"));
}
