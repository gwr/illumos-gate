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
 * Characterize NOT_REACHED path termination.  The cases distinguish code
 * after the marker from state merged from live and terminated branches.
 */

#ifdef __lock_lint
#include <sys/mutex.h>
#include <sys/note.h>
#else
#define	_NOTE(arg)

typedef struct kmutex {
	int opaque;
} kmutex_t;
#endif

struct protected_state {
	kmutex_t lock;
	int value;
};

_NOTE(MUTEX_PROTECTS_DATA(protected_state::lock, protected_state::value))

#ifndef __lock_lint
extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
#endif

static void
access_after_not_reached(struct protected_state *state)
{
	_NOTE(NOT_REACHED)
	state->value = 1;
}

static void
terminated_locked_branch(struct protected_state *state, int terminate)
{
	if (terminate) {
		mutex_enter(&state->lock);
		_NOTE(NOT_REACHED)
	}
	state->value = 1;
}

static void
only_locked_path_reaches_access(struct protected_state *state, int terminate)
{
	if (terminate) {
		_NOTE(NOT_REACHED)
	} else {
		mutex_enter(&state->lock);
	}
	state->value = 1;
	mutex_exit(&state->lock);
}

static void
balanced_live_path(struct protected_state *state, int terminate)
{
	if (terminate) {
		mutex_enter(&state->lock);
		_NOTE(NOT_REACHED)
	}
	mutex_enter(&state->lock);
	state->value = 1;
	mutex_exit(&state->lock);
}
