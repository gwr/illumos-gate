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
 * Define protected data shared by the command-file readable-policy tests.
 * Repeated inclusion gives Sparse a distinct type instance in each
 * translation unit, as occurs with structures declared in kernel headers.
 */

#ifndef COMMAND_READABLE_H
#define	COMMAND_READABLE_H

#define	_NOTE(arg)

typedef struct command_mutex {
	int opaque;
} command_mutex_t;

typedef struct command_state {
	command_mutex_t lock;
	int readable;
	struct {
		int first;
		int second;
	} readable_group;
} command_state_t;

extern command_state_t command_object;
extern command_mutex_t command_global_lock;
extern int command_global;

int command_readable_primary(void);
int command_readable_other(command_state_t *);

_NOTE(MUTEX_PROTECTS_DATA(command_state::lock,
    command_state::{ readable readable_group }))
_NOTE(MUTEX_PROTECTS_DATA(command_global_lock, command_global))

#endif /* COMMAND_READABLE_H */
