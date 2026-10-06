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
 * Verify command-declared readers-writer protection for type members and
 * global objects.  Source policies provide duplicate and cross-mechanism
 * conflict cases.
 */

#define	_NOTE(arg)

typedef struct rwlock {
	int opaque;
} rwlock_t;

typedef struct mutex {
	int opaque;
} mutex_t;

struct command_rwlock_state {
	rwlock_t lock;
	rwlock_t alternate;
	mutex_t mutex;
	int value;
	int duplicate;
	int mutex_value;
};

rwlock_t command_rwlock_global_lock;
int command_rwlock_global_value;

_NOTE(RWLOCK_PROTECTS_DATA(command_rwlock_state::lock,
    command_rwlock_state::duplicate))
_NOTE(MUTEX_PROTECTS_DATA(command_rwlock_state::mutex,
    command_rwlock_state::mutex_value))

extern int rw_rdlock(rwlock_t *);
extern int rw_wrlock(rwlock_t *);
extern int rw_unlock(rwlock_t *);

int
command_rwlock_access(struct command_rwlock_state *state)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)

	value = state->value;
	state->value = value;

	(void) rw_rdlock(&state->lock);
	value += state->value;
	state->value = value;
	(void) rw_unlock(&state->lock);

	(void) rw_wrlock(&state->lock);
	state->value = value;
	(void) rw_unlock(&state->lock);

	value += command_rwlock_global_value;
	(void) rw_rdlock(&command_rwlock_global_lock);
	value += command_rwlock_global_value;
	(void) rw_unlock(&command_rwlock_global_lock);

	return (value);
}
