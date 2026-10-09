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

#ifndef COMMAND_NAMES_H
#define	COMMAND_NAMES_H

#include <stdbool.h>
#include <stddef.h>

struct command_name_list {
	char **names;
	size_t count;
	const char *error;
};

bool command_names_expand(size_t, const char *const *,
    struct command_name_list *);
void command_names_free(struct command_name_list *);

#endif /* COMMAND_NAMES_H */
