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
 * Characterize rw_tryupgrade() as a result-sensitive reader-to-writer
 * transition, including invalid inputs, ignored results, and lock order.
 */

#ifdef __lock_lint
#include <sys/note.h>
#include <sys/rwlock.h>
#else
#define	_NOTE(arg)

typedef struct krwlock {
	void *opaque[1];
} krwlock_t;

typedef enum krw {
	RW_WRITER,
	RW_READER
} krw_t;
#endif

struct tryupgrade_state {
	krwlock_t first;
	krwlock_t second;
	int value;
};

_NOTE(LOCK_ORDER(tryupgrade_state::first tryupgrade_state::second))
_NOTE(RWLOCK_PROTECTS_DATA(tryupgrade_state::first,
    tryupgrade_state::value))

#ifndef __lock_lint
extern void rw_enter(krwlock_t *, krw_t);
extern int rw_tryupgrade(krwlock_t *);
extern void rw_exit(krwlock_t *);
#endif

static int
tryupgrade_truth(struct tryupgrade_state *state)
{
	int value;

	rw_enter(&state->first, RW_READER);
	if (rw_tryupgrade(&state->first)) {
		value = state->value;
		state->value = value;
	} else {
		value = state->value;
		state->value = value;
	}
	rw_exit(&state->first);
	return (value);
}

static void
tryupgrade_negated(struct tryupgrade_state *state)
{
	rw_enter(&state->first, RW_READER);
	if (!rw_tryupgrade(&state->first))
		state->value = 1;
	else
		state->value = 2;
	rw_exit(&state->first);
}

static void
tryupgrade_equal_zero(struct tryupgrade_state *state)
{
	rw_enter(&state->first, RW_READER);
	if (rw_tryupgrade(&state->first) == 0)
		state->value = 1;
	else
		state->value = 2;
	rw_exit(&state->first);
}

static int
tryupgrade_saved_result(struct tryupgrade_state *state)
{
	int upgraded;
	int value;

	rw_enter(&state->first, RW_READER);
	upgraded = rw_tryupgrade(&state->first);
	if (upgraded != 0)
		state->value = 1;
	value = state->value;
	rw_exit(&state->first);
	return (value);
}

static int
tryupgrade_ignored_result(struct tryupgrade_state *state)
{
	int value;

	rw_enter(&state->first, RW_READER);
	(void) rw_tryupgrade(&state->first);
	value = state->value;
	state->value = value;
	rw_exit(&state->first);
	return (value);
}

static void
balanced_tryupgrade(struct tryupgrade_state *state)
{
	rw_enter(&state->first, RW_READER);
	if (rw_tryupgrade(&state->first))
		state->value = 1;
	rw_exit(&state->first);
}

static void
call_balanced_tryupgrade(struct tryupgrade_state *state)
{
	balanced_tryupgrade(state);
	state->value = 1;
}

static void
tryupgrade_order(struct tryupgrade_state *state)
{
	rw_enter(&state->first, RW_READER);
	rw_enter(&state->second, RW_WRITER);
	if (rw_tryupgrade(&state->first))
		state->value = 1;
	rw_exit(&state->first);
	rw_exit(&state->second);
}

static void
tryupgrade_unheld(struct tryupgrade_state *state)
{
	if (rw_tryupgrade(&state->first))
		state->value = 1;
	rw_exit(&state->first);
}

static void
tryupgrade_writer(struct tryupgrade_state *state)
{
	rw_enter(&state->first, RW_WRITER);
	if (rw_tryupgrade(&state->first))
		state->value = 1;
	rw_exit(&state->first);
}
