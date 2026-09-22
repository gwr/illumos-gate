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
 * Characterize diagnostics for locks other than the released and reacquired
 * mutex that remain held across a condition-variable wait.
 */

#ifdef __lock_lint
#include <sys/condvar.h>
#include <sys/mutex.h>
#else
typedef struct kmutex {
	int opaque;
} kmutex_t;

typedef struct kcondvar {
	int opaque;
} kcondvar_t;
#endif

struct wait_other_state {
	kmutex_t wait_lock;
	kmutex_t other_first;
	kmutex_t other_second;
	kcondvar_t cv;
};

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void cv_wait(kcondvar_t *, kmutex_t *);
void wait_without_other_lock(struct wait_other_state *);
void wait_with_one_other_lock(struct wait_other_state *);
void wait_with_two_other_locks(struct wait_other_state *);
void wait_with_maybe_other_lock(struct wait_other_state *, int);
void wait_with_other_lock_wrapped(struct wait_other_state *);

#ifndef WAIT_OTHER_VARIANT
#define	WAIT_OTHER_VARIANT	0
#endif

#if WAIT_OTHER_VARIANT == 0 || WAIT_OTHER_VARIANT == 1
void
wait_without_other_lock(struct wait_other_state *state)
{
	mutex_enter(&state->wait_lock);
	cv_wait(&state->cv, &state->wait_lock);
	mutex_exit(&state->wait_lock);
}
#endif

#if WAIT_OTHER_VARIANT == 0 || WAIT_OTHER_VARIANT == 2
void
wait_with_one_other_lock(struct wait_other_state *state)
{
	mutex_enter(&state->other_first);
	mutex_enter(&state->wait_lock);
	cv_wait(&state->cv, &state->wait_lock);
	mutex_exit(&state->wait_lock);
	mutex_exit(&state->other_first);
}
#endif

#if WAIT_OTHER_VARIANT == 0 || WAIT_OTHER_VARIANT == 3
void
wait_with_two_other_locks(struct wait_other_state *state)
{
	mutex_enter(&state->other_first);
	mutex_enter(&state->other_second);
	mutex_enter(&state->wait_lock);
	cv_wait(&state->cv, &state->wait_lock);
	mutex_exit(&state->wait_lock);
	mutex_exit(&state->other_second);
	mutex_exit(&state->other_first);
}
#endif

#if WAIT_OTHER_VARIANT == 0 || WAIT_OTHER_VARIANT == 4
void
wait_with_maybe_other_lock(struct wait_other_state *state, int take_other)
{
	if (take_other)
		mutex_enter(&state->other_first);
	mutex_enter(&state->wait_lock);
	cv_wait(&state->cv, &state->wait_lock);
	mutex_exit(&state->wait_lock);
	if (take_other)
		mutex_exit(&state->other_first);
}
#endif

#if WAIT_OTHER_VARIANT == 0 || WAIT_OTHER_VARIANT == 5
static void
wait_other_helper(struct wait_other_state *state)
{
	cv_wait(&state->cv, &state->wait_lock);
}

void
wait_with_other_lock_wrapped(struct wait_other_state *state)
{
	mutex_enter(&state->other_first);
	mutex_enter(&state->wait_lock);
	wait_other_helper(state);
	mutex_exit(&state->wait_lock);
	mutex_exit(&state->other_first);
}
#endif
