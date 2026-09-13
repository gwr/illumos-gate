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
 * Parse declarative locklint command files and dispatch their commands.
 * The program using this parser supplies one handler for each accepted
 * top-level command.
 */

#ifndef COMMAND_PARSE_H
#define	COMMAND_PARSE_H

int command_parse_file(const char *);
int command_parse_error(const char *, ...);
const char *command_parse_path(void);
unsigned long command_parse_line(void);

int cmd_assert(int, char **);
int cmd_declare(int, char **);
int cmd_ignore(int, char **);

#endif /* COMMAND_PARSE_H */
