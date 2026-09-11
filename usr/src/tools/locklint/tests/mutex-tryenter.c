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
 * Characterize mutex_tryenter() as a result-sensitive acquisition.  The
 * cases cover common predicate spellings, saved results, ignored results,
 * lock ordering and already-held failure.
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

struct tryenter_state {
	kmutex_t first;
	kmutex_t second;
	int value;
};

_NOTE(LOCK_ORDER(tryenter_state::first tryenter_state::second))
_NOTE(MUTEX_PROTECTS_DATA(tryenter_state::first, tryenter_state::value))

#ifndef __lock_lint
extern void mutex_enter(kmutex_t *);
extern int mutex_tryenter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
#endif

static int
tryenter_truth(struct tryenter_state *state)
{
	int value = 0;

	if (mutex_tryenter(&state->first)) {
		value = state->value;
		mutex_exit(&state->first);
	}
	return (value);
}

static void
call_balanced_tryenter(struct tryenter_state *state)
{
	(void) tryenter_truth(state);
	state->value = 1;
}

static int
tryenter_negated(struct tryenter_state *state)
{
	int value = 0;

	if (!mutex_tryenter(&state->first))
		value = state->value;
	else
		mutex_exit(&state->first);
	return (value);
}

static int
tryenter_equal_zero(struct tryenter_state *state)
{
	int value = 0;

	if (mutex_tryenter(&state->first) == 0)
		value = state->value;
	else
		mutex_exit(&state->first);
	return (value);
}

static int
tryenter_not_equal_zero(struct tryenter_state *state)
{
	int value = 0;

	if (mutex_tryenter(&state->first) != 0) {
		value = state->value;
		mutex_exit(&state->first);
	}
	return (value);
}

static int
tryenter_saved_result(struct tryenter_state *state)
{
	int acquired;
	int value = 0;

	acquired = mutex_tryenter(&state->first);
	if (acquired != 0) {
		value = state->value;
		mutex_exit(&state->first);
	}
	return (value);
}

static void
tryenter_ignored_result(struct tryenter_state *state)
{
	(void) mutex_tryenter(&state->first);
	state->value = 1;
	mutex_exit(&state->first);
}

static void
tryenter_order_on_success(struct tryenter_state *state)
{
	mutex_enter(&state->second);
	if (mutex_tryenter(&state->first))
		mutex_exit(&state->first);
	mutex_exit(&state->second);
}

static int
tryenter_already_held(struct tryenter_state *state)
{
	int acquired;

	mutex_enter(&state->first);
	acquired = mutex_tryenter(&state->first);
	mutex_exit(&state->first);
	return (acquired);
}

static int
tryenter_already_held_branch(struct tryenter_state *state)
{
	int value;

	mutex_enter(&state->first);
	if (mutex_tryenter(&state->first))
		mutex_exit(&state->first);
	value = state->value;
	mutex_exit(&state->first);
	return (value);
}
