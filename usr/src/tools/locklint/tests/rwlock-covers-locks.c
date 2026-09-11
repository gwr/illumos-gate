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
 * Characterize RWLOCK_COVERS_LOCKS protection and ownership rules.  The
 * cases distinguish cover modes, object identity, and wrapper propagation.
 */

#ifdef __lock_lint
#include <sys/mutex.h>
#include <sys/note.h>
#include <sys/rwlock.h>
#else
#define	_NOTE(arg)

typedef struct kmutex {
	void *opaque[1];
} kmutex_t;

typedef struct krwlock {
	void *opaque[1];
} krwlock_t;

typedef enum krw {
	RW_WRITER,
	RW_READER
} krw_t;
#endif

struct covered_state {
	krwlock_t cover;
	kmutex_t inner;
	int value;
};

static struct covered_state first;
static struct covered_state second;
static krwlock_t absolute_cover;
static kmutex_t absolute_inner;
static int absolute_value;

_NOTE(MUTEX_PROTECTS_DATA(covered_state::inner, covered_state::value))
_NOTE(RWLOCK_COVERS_LOCKS(covered_state::cover, covered_state::inner))
_NOTE(MUTEX_PROTECTS_DATA(absolute_inner, absolute_value))
_NOTE(RWLOCK_COVERS_LOCKS(absolute_cover, absolute_inner))

#ifndef __lock_lint
extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void rw_enter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
#endif

static int
access_with_cover_writer(struct covered_state *state)
{
	int value;

	rw_enter(&state->cover, RW_WRITER);
	value = state->value;
	state->value = value;
	rw_exit(&state->cover);
	return (value);
}

static void
access_with_cover_reader(struct covered_state *state)
{
	rw_enter(&state->cover, RW_READER);
	state->value = 1;
	rw_exit(&state->cover);
}

static void
acquire_with_cover_reader(struct covered_state *state)
{
	rw_enter(&state->cover, RW_READER);
	mutex_enter(&state->inner);
	mutex_exit(&state->inner);
	rw_exit(&state->cover);
}

static void
acquire_with_cover_writer(struct covered_state *state)
{
	rw_enter(&state->cover, RW_WRITER);
	mutex_enter(&state->inner);
	mutex_exit(&state->inner);
	rw_exit(&state->cover);
}

static void
acquire_without_cover(struct covered_state *state)
{
	mutex_enter(&state->inner);
	mutex_exit(&state->inner);
}

static void
release_cover_while_covered(struct covered_state *state)
{
	rw_enter(&state->cover, RW_READER);
	mutex_enter(&state->inner);
	rw_exit(&state->cover);
	mutex_exit(&state->inner);
}

static void
acquire_other_object(void)
{
	rw_enter(&first.cover, RW_READER);
	mutex_enter(&second.inner);
	mutex_exit(&second.inner);
	rw_exit(&first.cover);
}

static void
acquire_inner(struct covered_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->inner))
	mutex_enter(&state->inner);
}

static void
call_wrapper_with_cover(struct covered_state *state)
{
	rw_enter(&state->cover, RW_READER);
	acquire_inner(state);
	mutex_exit(&state->inner);
	rw_exit(&state->cover);
}

static void
call_wrapper_without_cover(struct covered_state *state)
{
	acquire_inner(state);
	mutex_exit(&state->inner);
}

static void
release_cover(struct covered_state *state)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->cover))
	rw_exit(&state->cover);
}

static void
call_release_wrapper_while_covered(struct covered_state *state)
{
	rw_enter(&state->cover, RW_READER);
	mutex_enter(&state->inner);
	release_cover(state);
	mutex_exit(&state->inner);
}

static void
write_value(struct covered_state *state)
{
	state->value = 1;
}

static void
call_access_wrapper_with_writer(struct covered_state *state)
{
	rw_enter(&state->cover, RW_WRITER);
	write_value(state);
	rw_exit(&state->cover);
}

static void
call_access_wrapper_with_reader(struct covered_state *state)
{
	rw_enter(&state->cover, RW_READER);
	write_value(state);
	rw_exit(&state->cover);
}

static int
access_absolute_cover_writer(void)
{
	int value;

	rw_enter(&absolute_cover, RW_WRITER);
	value = absolute_value;
	absolute_value = value;
	rw_exit(&absolute_cover);
	return (value);
}

static void
acquire_absolute_without_cover(void)
{
	mutex_enter(&absolute_inner);
	mutex_exit(&absolute_inner);
}
