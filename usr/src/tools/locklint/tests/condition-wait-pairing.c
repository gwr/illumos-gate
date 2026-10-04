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
 * Characterize the requirement that one condition variable be used
 * consistently with one mutex.  Separately compiled variants isolate a
 * consistent pair, two independent pairs, and one condition variable paired
 * with two different mutexes.
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

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void cv_wait(kcondvar_t *, kmutex_t *);

static kmutex_t first;
static kmutex_t second;
static kcondvar_t shared_cv;
static kcondvar_t second_cv;

struct wait_pairing_state {
	kmutex_t first;
	kmutex_t second;
};

#ifndef WAIT_PAIRING_VARIANT
#define	WAIT_PAIRING_VARIANT	0
#endif

#if WAIT_PAIRING_VARIANT == 0 || WAIT_PAIRING_VARIANT == 1
void
wait_pairing_consistent(void)
{
	mutex_enter(&first);
	cv_wait(&shared_cv, &first);
	cv_wait(&shared_cv, &first);
	mutex_exit(&first);
}
#endif

#if WAIT_PAIRING_VARIANT == 0 || WAIT_PAIRING_VARIANT == 2
void
wait_pairing_independent(void)
{
	mutex_enter(&first);
	cv_wait(&shared_cv, &first);
	mutex_exit(&first);

	mutex_enter(&second);
	cv_wait(&second_cv, &second);
	mutex_exit(&second);
}
#endif

#if WAIT_PAIRING_VARIANT == 0 || WAIT_PAIRING_VARIANT == 3
void
wait_pairing_mismatch(void)
{
	mutex_enter(&first);
	cv_wait(&shared_cv, &first);
	mutex_exit(&first);

	mutex_enter(&second);
	cv_wait(&shared_cv, &second);
	mutex_exit(&second);
}
#endif

#if WAIT_PAIRING_VARIANT == 0
static kcondvar_t wrapped_cv;

static void
wait_pairing_first_helper(kcondvar_t *cv, kmutex_t *mutex)
{
	cv_wait(cv, mutex);
}

static void
wait_pairing_second_helper(kcondvar_t *cv, kmutex_t *mutex)
{
	cv_wait(cv, mutex);
}

void
wait_pairing_wrapped_mismatch(struct wait_pairing_state *state)
{
	mutex_enter(&state->first);
	wait_pairing_first_helper(&wrapped_cv, &state->first);
	mutex_exit(&state->first);

	mutex_enter(&state->second);
	wait_pairing_second_helper(&wrapped_cv, &state->second);
	mutex_exit(&state->second);
}
#endif
