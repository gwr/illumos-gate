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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Verify object-specific mutex protection declared by command.  The source
 * declaration provides duplicate and conflict cases while command-only
 * member and global policies establish analysis behavior.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct command_mutex_state {
	mutex_t lock;
	mutex_t alternate;
	int value;
	int second;
	int source_value;
};

struct command_mutex_state command_mutex_object;
mutex_t command_mutex_global_lock;
int command_mutex_global_value;

_NOTE(MUTEX_PROTECTS_DATA(command_mutex_object.lock,
    command_mutex_object.source_value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

int
command_mutex_access(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)

	value = command_mutex_object.value;
	mutex_enter(&command_mutex_object.lock);
	value += command_mutex_object.value;
	mutex_exit(&command_mutex_object.lock);

	value += command_mutex_object.second;
	value += command_mutex_global_value;
	mutex_enter(&command_mutex_global_lock);
	value += command_mutex_global_value;
	mutex_exit(&command_mutex_global_lock);

	return (value);
}
