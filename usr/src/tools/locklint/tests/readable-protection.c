/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify that a permitted unlocked read does not dilute the common lock
 * observed across writes to readable data.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct readable_state {
	mutex_t lock;
	int value;
} readable_state_t;

_NOTE(MUTEX_PROTECTS_DATA(readable_state::lock, readable_state::value))
_NOTE(DATA_READABLE_WITHOUT_LOCK(readable_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

int
readable_protection(readable_state_t *state)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = state->value;
	mutex_enter(&state->lock);
	state->value = value;
	mutex_exit(&state->lock);
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}
