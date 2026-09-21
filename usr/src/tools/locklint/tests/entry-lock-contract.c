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
 * Characterize entry lock contracts independently from root selection.
 * The helper preserves its asserted entry state, while the callers and the
 * deliberate leak distinguish requirement propagation from return effects.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#include <sys/note.h>
#include <synch.h>
#else
#define	_NOTE(arg)
#define	ASSERT(expr)
#define	MUTEX_HELD(lock)	mutex_owned(lock)

typedef struct mutex {
	int opaque;
} mutex_t;
#endif

struct entry_lock_state {
	mutex_t lock;
	int value;
};

#ifndef ENTRY_LOCK_VARIANT
#define	ENTRY_LOCK_VARIANT	0
#endif

_NOTE(MUTEX_PROTECTS_DATA(entry_lock_state::lock,
    entry_lock_state::value))

extern int mutex_owned(mutex_t *);
extern int mutex_lock(mutex_t *);
extern int mutex_unlock(mutex_t *);

#ifdef ENTRY_LOCK_HELPER_STATIC
#define	ENTRY_LOCK_HELPER_SCOPE	static
#else
#define	ENTRY_LOCK_HELPER_SCOPE
#endif

#if ENTRY_LOCK_VARIANT != 4

ENTRY_LOCK_HELPER_SCOPE void entry_lock_helper(struct entry_lock_state *);

#endif

#if ENTRY_LOCK_VARIANT == 0 || ENTRY_LOCK_VARIANT == 1

void entry_lock_correct_caller(struct entry_lock_state *);

#endif

#if ENTRY_LOCK_VARIANT == 0 || ENTRY_LOCK_VARIANT == 2

void entry_lock_incorrect_caller(struct entry_lock_state *);

#endif

#if ENTRY_LOCK_VARIANT == 0 || ENTRY_LOCK_VARIANT == 4

void entry_lock_leak(struct entry_lock_state *);

#endif

#if ENTRY_LOCK_VARIANT != 4

ENTRY_LOCK_HELPER_SCOPE void
entry_lock_helper(struct entry_lock_state *state)
{
	ASSERT(MUTEX_HELD(&state->lock));
	state->value++;
	(void) mutex_unlock(&state->lock);
	(void) mutex_lock(&state->lock);
	state->value++;
}

#endif

#if ENTRY_LOCK_VARIANT == 0 || ENTRY_LOCK_VARIANT == 1

void
entry_lock_correct_caller(struct entry_lock_state *state)
{
	(void) mutex_lock(&state->lock);
	entry_lock_helper(state);
	(void) mutex_unlock(&state->lock);
}

#endif

#if ENTRY_LOCK_VARIANT == 0 || ENTRY_LOCK_VARIANT == 2

void
entry_lock_incorrect_caller(struct entry_lock_state *state)
{
	entry_lock_helper(state);
}

#endif

#if ENTRY_LOCK_VARIANT == 0 || ENTRY_LOCK_VARIANT == 4

void
entry_lock_leak(struct entry_lock_state *state)
{
	(void) mutex_lock(&state->lock);
}

#endif
