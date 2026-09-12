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
	(void) argc;
	(void) argv;
	return (not_implemented("declare"));
}

int
cmd_ignore(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	return (not_implemented("ignore"));
}
