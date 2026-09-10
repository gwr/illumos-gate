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
 * Characterize cv_wait() as a required-held mutex release and reacquisition.
 * The cases verify post-wait ownership and lock ordering while another mutex
 * remains held.
 */

#ifdef __lock_lint
#include <sys/condvar.h>
#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/note.h>
#else
#define	_NOTE(arg)
#define	ASSERT(expr)
#define	MUTEX_HELD(lock)	mutex_owned(lock)

typedef struct kmutex {
	int opaque;
} kmutex_t;

typedef struct kcondvar {
	int opaque;
} kcondvar_t;

typedef long clock_t;
typedef long long hrtime_t;
typedef int time_res_t;
typedef struct timestruc {
	long tv_sec;
	long tv_nsec;
} timestruc_t;
#endif

struct condition_wait_state {
	kmutex_t first;
	kmutex_t second;
	kcondvar_t cv_first;
	kcondvar_t cv_second;
	int value;
};

_NOTE(LOCK_ORDER(condition_wait_state::first condition_wait_state::second))
_NOTE(MUTEX_PROTECTS_DATA(condition_wait_state::first,
    condition_wait_state::value))

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern int mutex_owned(const kmutex_t *);
extern void cv_wait(kcondvar_t *, kmutex_t *);
extern int cv_wait_sig(kcondvar_t *, kmutex_t *);
extern clock_t cv_timedwait(kcondvar_t *, kmutex_t *, clock_t);
extern clock_t cv_timedwait_sig(kcondvar_t *, kmutex_t *, clock_t);
extern clock_t cv_reltimedwait(kcondvar_t *, kmutex_t *, clock_t, time_res_t);
extern clock_t cv_reltimedwait_sig(kcondvar_t *, kmutex_t *, clock_t,
    time_res_t);
extern void cv_wait_stop(kcondvar_t *, kmutex_t *, int);
extern clock_t cv_timedwait_hires(kcondvar_t *, kmutex_t *, hrtime_t,
    hrtime_t, int);
extern int cv_timedwait_sig_hrtime(kcondvar_t *, kmutex_t *, hrtime_t);
extern int cv_wait_sig_swap(kcondvar_t *, kmutex_t *);
extern int cv_wait_sig_swap_core(kcondvar_t *, kmutex_t *, int *);
extern int cv_waituntil_sig(kcondvar_t *, kmutex_t *, timestruc_t *, int);

static void
wait_wrapper(kcondvar_t *cv, kmutex_t *mutex)
{
	ASSERT(MUTEX_HELD(mutex));
	cv_wait(cv, mutex);
}

static int
wait_while_held(struct condition_wait_state *state)
{
	int value;

	mutex_enter(&state->first);
	cv_wait(&state->cv_first, &state->first);
	value = state->value;
	mutex_exit(&state->first);
	return (value);
}

static void
wait_without_lock(struct condition_wait_state *state)
{
	cv_wait(&state->cv_first, &state->first);
	mutex_exit(&state->first);
}

static int
wait_variants_while_held(struct condition_wait_state *state)
{
	int value = 0;

	mutex_enter(&state->first);
	if (cv_wait_sig(&state->cv_first, &state->first) == 0)
		value += state->value;
	if (cv_timedwait(&state->cv_first, &state->first, 1) < 0)
		value += state->value;
	if (cv_timedwait_sig(&state->cv_first, &state->first, 1) <= 0)
		value += state->value;
	if (cv_reltimedwait(&state->cv_first, &state->first, 1, 0) < 0)
		value += state->value;
	if (cv_reltimedwait_sig(&state->cv_first, &state->first, 1, 0) <= 0)
		value += state->value;
	mutex_exit(&state->first);
	return (value);
}

static void
wait_sig_without_lock(struct condition_wait_state *state)
{
	(void) cv_wait_sig(&state->cv_first, &state->first);
	mutex_exit(&state->first);
}

static void
timedwait_without_lock(struct condition_wait_state *state)
{
	(void) cv_timedwait(&state->cv_first, &state->first, 1);
	mutex_exit(&state->first);
}

static void
timedwait_sig_without_lock(struct condition_wait_state *state)
{
	(void) cv_timedwait_sig(&state->cv_first, &state->first, 1);
	mutex_exit(&state->first);
}

static void
reltimedwait_without_lock(struct condition_wait_state *state)
{
	(void) cv_reltimedwait(&state->cv_first, &state->first, 1, 0);
	mutex_exit(&state->first);
}

static void
reltimedwait_sig_without_lock(struct condition_wait_state *state)
{
	(void) cv_reltimedwait_sig(&state->cv_first, &state->first, 1, 0);
	mutex_exit(&state->first);
}

static int
uncommon_waits_while_held(struct condition_wait_state *state)
{
	timestruc_t when = { 1, 0 };
	int sigret = 0;
	int value = 0;

	mutex_enter(&state->first);
	cv_wait_stop(&state->cv_first, &state->first, 1);
	if (cv_timedwait_hires(&state->cv_first, &state->first, 1, 1, 0) < 0)
		value += state->value;
	if (cv_timedwait_sig_hrtime(&state->cv_first, &state->first, 1) <= 0)
		value += state->value;
	if (cv_wait_sig_swap(&state->cv_first, &state->first) == 0)
		value += state->value;
	if (cv_wait_sig_swap_core(&state->cv_first, &state->first,
	    &sigret) == 0)
		value += state->value;
	if (cv_waituntil_sig(&state->cv_first, &state->first, &when, 0) <= 0)
		value += state->value;
	mutex_exit(&state->first);
	return (value);
}

static void
wait_stop_without_lock(struct condition_wait_state *state)
{
	cv_wait_stop(&state->cv_first, &state->first, 1);
	mutex_exit(&state->first);
}

static void
timedwait_hires_without_lock(struct condition_wait_state *state)
{
	(void) cv_timedwait_hires(&state->cv_first, &state->first, 1, 1, 0);
	mutex_exit(&state->first);
}

static void
timedwait_sig_hrtime_without_lock(struct condition_wait_state *state)
{
	(void) cv_timedwait_sig_hrtime(&state->cv_first, &state->first, 1);
	mutex_exit(&state->first);
}

static void
wait_sig_swap_without_lock(struct condition_wait_state *state)
{
	(void) cv_wait_sig_swap(&state->cv_first, &state->first);
	mutex_exit(&state->first);
}

static void
wait_sig_swap_core_without_lock(struct condition_wait_state *state)
{
	int sigret;

	(void) cv_wait_sig_swap_core(&state->cv_first, &state->first, &sigret);
	mutex_exit(&state->first);
}

static void
waituntil_sig_without_lock(struct condition_wait_state *state)
{
	timestruc_t when = { 1, 0 };

	(void) cv_waituntil_sig(&state->cv_first, &state->first, &when, 0);
	mutex_exit(&state->first);
}

static int
wrapped_wait_while_held(struct condition_wait_state *state)
{
	int value;

	mutex_enter(&state->first);
	wait_wrapper(&state->cv_first, &state->first);
	value = state->value;
	mutex_exit(&state->first);
	return (value);
}

static void
wrapped_wait_reacquire_inversion(struct condition_wait_state *state)
{
	mutex_enter(&state->first);
	mutex_enter(&state->second);
	wait_wrapper(&state->cv_first, &state->first);
	mutex_exit(&state->second);
	mutex_exit(&state->first);
}

static void
wait_reacquire_in_order(struct condition_wait_state *state)
{
	mutex_enter(&state->first);
	mutex_enter(&state->second);
	cv_wait(&state->cv_second, &state->second);
	mutex_exit(&state->second);
	mutex_exit(&state->first);
}

static void
wait_reacquire_inversion(struct condition_wait_state *state)
{
	mutex_enter(&state->first);
	mutex_enter(&state->second);
	cv_wait(&state->cv_first, &state->first);
	mutex_exit(&state->second);
	mutex_exit(&state->first);
}

static void
timedwait_sig_reacquire_inversion(struct condition_wait_state *state)
{
	mutex_enter(&state->first);
	mutex_enter(&state->second);
	(void) cv_timedwait_sig(&state->cv_first, &state->first, 1);
	mutex_exit(&state->second);
	mutex_exit(&state->first);
}

static void
wait_sig_swap_reacquire_inversion(struct condition_wait_state *state)
{
	mutex_enter(&state->first);
	mutex_enter(&state->second);
	(void) cv_wait_sig_swap(&state->cv_first, &state->first);
	mutex_exit(&state->second);
	mutex_exit(&state->first);
}
