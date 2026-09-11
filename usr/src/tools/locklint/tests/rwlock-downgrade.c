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
 * Characterize rw_downgrade() as a required writer-held to reader-held
 * transition.  The cases distinguish entry modes, post-transition access,
 * and propagation through a wrapper.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#include <sys/note.h>
#include <sys/rwlock.h>
#else
#define	_NOTE(arg)
#define	ASSERT(expr)
#define	RW_WRITE_HELD(lock)	rw_write_held(lock)

typedef struct krwlock {
	void *opaque[1];
} krwlock_t;

typedef enum krw {
	RW_WRITER,
	RW_READER
} krw_t;
#endif

struct downgrade_state {
	krwlock_t lock;
	int value;
};

_NOTE(RWLOCK_PROTECTS_DATA(downgrade_state::lock, downgrade_state::value))

#ifndef __lock_lint
extern void rw_enter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
extern void rw_downgrade(krwlock_t *);
extern int rw_write_held(krwlock_t *);
#endif

static void
downgrade_wrapper(krwlock_t *lock)
{
	ASSERT(RW_WRITE_HELD(lock));
	rw_downgrade(lock);
}

static int
downgrade_writer(struct downgrade_state *state)
{
	int value;

	rw_enter(&state->lock, RW_WRITER);
	rw_downgrade(&state->lock);
	value = state->value;
	state->value = value;
	rw_exit(&state->lock);
	return (value);
}

static void
downgrade_reader(struct downgrade_state *state)
{
	rw_enter(&state->lock, RW_READER);
	rw_downgrade(&state->lock);
	rw_exit(&state->lock);
}

static void
downgrade_unheld(struct downgrade_state *state)
{
	rw_downgrade(&state->lock);
	rw_exit(&state->lock);
}

static int
downgrade_wrapped(struct downgrade_state *state)
{
	int value;

	rw_enter(&state->lock, RW_WRITER);
	downgrade_wrapper(&state->lock);
	value = state->value;
	state->value = value;
	rw_exit(&state->lock);
	return (value);
}
