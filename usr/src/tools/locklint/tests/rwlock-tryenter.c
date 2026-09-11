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
 * Characterize result-sensitive rw_tryenter() reader and writer
 * acquisitions, including ignored results, ordering, and held inputs.
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
	RW_READER,
	RW_READER_STARVEWRITER
} krw_t;
#endif

struct rw_tryenter_state {
	krwlock_t first;
	krwlock_t second;
	int value;
};

_NOTE(LOCK_ORDER(rw_tryenter_state::first rw_tryenter_state::second))
_NOTE(RWLOCK_PROTECTS_DATA(rw_tryenter_state::first,
    rw_tryenter_state::value))

#ifndef __lock_lint
extern void rw_enter(krwlock_t *, krw_t);
extern int rw_tryenter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
#endif

static int
tryenter_reader_truth(struct rw_tryenter_state *state)
{
	int value = 0;

	if (rw_tryenter(&state->first, RW_READER)) {
		value = state->value;
		rw_exit(&state->first);
	}
	return (value);
}

static void
tryenter_reader_write(struct rw_tryenter_state *state)
{
	if (rw_tryenter(&state->first, RW_READER)) {
		state->value = 1;
		rw_exit(&state->first);
	}
}

static int
tryenter_starvewriter_truth(struct rw_tryenter_state *state)
{
	int value = 0;

	if (rw_tryenter(&state->first, RW_READER_STARVEWRITER)) {
		value = state->value;
		rw_exit(&state->first);
	}
	return (value);
}

static int
tryenter_writer_truth(struct rw_tryenter_state *state)
{
	int value = 0;

	if (rw_tryenter(&state->first, RW_WRITER)) {
		value = state->value;
		state->value = value;
		rw_exit(&state->first);
	}
	return (value);
}

static int
tryenter_reader_failure(struct rw_tryenter_state *state)
{
	int value = 0;

	if (!rw_tryenter(&state->first, RW_READER))
		value = state->value;
	else
		rw_exit(&state->first);
	return (value);
}

static int
tryenter_writer_failure(struct rw_tryenter_state *state)
{
	int value = 0;

	if (!rw_tryenter(&state->first, RW_WRITER))
		state->value = value;
	else
		rw_exit(&state->first);
	return (value);
}

static int
tryenter_saved_result(struct rw_tryenter_state *state)
{
	int acquired;
	int value = 0;

	acquired = rw_tryenter(&state->first, RW_WRITER);
	if (acquired != 0) {
		value = state->value;
		rw_exit(&state->first);
	}
	return (value);
}

static void
tryenter_ignored_result(struct rw_tryenter_state *state)
{
	(void) rw_tryenter(&state->first, RW_WRITER);
	state->value = 1;
	rw_exit(&state->first);
}

static void
call_balanced_tryenter(struct rw_tryenter_state *state)
{
	(void) tryenter_writer_truth(state);
	state->value = 1;
}

static void
tryenter_order_on_success(struct rw_tryenter_state *state)
{
	rw_enter(&state->second, RW_WRITER);
	if (rw_tryenter(&state->first, RW_WRITER))
		rw_exit(&state->first);
	rw_exit(&state->second);
}

static int
tryenter_reader_while_reader(struct rw_tryenter_state *state)
{
	int value;

	rw_enter(&state->first, RW_READER);
	if (rw_tryenter(&state->first, RW_READER))
		rw_exit(&state->first);
	value = state->value;
	rw_exit(&state->first);
	return (value);
}

static int
tryenter_writer_while_reader(struct rw_tryenter_state *state)
{
	int value;

	rw_enter(&state->first, RW_READER);
	if (rw_tryenter(&state->first, RW_WRITER)) {
		state->value = 1;
		rw_exit(&state->first);
	}
	value = state->value;
	rw_exit(&state->first);
	return (value);
}

static int
tryenter_reader_while_writer(struct rw_tryenter_state *state)
{
	int value;

	rw_enter(&state->first, RW_WRITER);
	if (rw_tryenter(&state->first, RW_READER))
		rw_exit(&state->first);
	value = state->value;
	rw_exit(&state->first);
	return (value);
}

static int
tryenter_writer_while_writer(struct rw_tryenter_state *state)
{
	int value;

	rw_enter(&state->first, RW_WRITER);
	if (rw_tryenter(&state->first, RW_WRITER))
		rw_exit(&state->first);
	value = state->value;
	rw_exit(&state->first);
	return (value);
}
