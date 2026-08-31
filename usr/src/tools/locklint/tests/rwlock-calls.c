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
 * Verify readers-writer lock conditions and effects across calls.  The same
 * read and write callees are invoked under reader and writer ownership to
 * distinguish their required modes; separate acquire helpers then prove that
 * reader and writer effects propagate with their mode intact to the caller.
 */

#ifdef __lock_lint
#include <sys/note.h>
#ifdef _KERNEL
#include <sys/rwlock.h>
#else
#include <synch.h>
#endif
#else
#define	_NOTE(arg)

#ifdef _KERNEL
typedef struct _krwlock {
	void *_opaque[1];
} krwlock_t;

typedef enum krw {
	RW_WRITER,
	RW_READER
} krw_t;
#else
typedef struct rwlock {
	void *_opaque[1];
} rwlock_t;
#endif
#endif

#ifdef _KERNEL
typedef krwlock_t test_rwlock_t;
#define	RWLOCK_READ_ENTER(lock)	rw_enter((lock), RW_READER)
#define	RWLOCK_WRITE_ENTER(lock)	rw_enter((lock), RW_WRITER)
#define	RWLOCK_EXIT(lock)	rw_exit(lock)
#else
typedef rwlock_t test_rwlock_t;
#define	RWLOCK_READ_ENTER(lock)	((void) rw_rdlock(lock))
#define	RWLOCK_WRITE_ENTER(lock)	((void) rw_wrlock(lock))
#define	RWLOCK_EXIT(lock)	((void) rw_unlock(lock))
#endif

typedef struct rwlock_call_state {
	test_rwlock_t lock;
	int value;
} rwlock_call_state_t;

_NOTE(RWLOCK_PROTECTS_DATA(rwlock_call_state::lock,
    rwlock_call_state::value))

#ifndef __lock_lint
#ifdef _KERNEL
extern void rw_enter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
#else
extern int rw_rdlock(rwlock_t *);
extern int rw_wrlock(rwlock_t *);
extern int rw_unlock(rwlock_t *);
#endif
#endif

static int
read_value(rwlock_call_state_t *state)
{
	return (state->value);
}

static void
write_value(rwlock_call_state_t *state)
{
	state->value = 1;
}

static int
call_read_as_reader(rwlock_call_state_t *state)
{
	int value;

	RWLOCK_READ_ENTER(&state->lock);
	value = read_value(state);
	RWLOCK_EXIT(&state->lock);
	return (value);
}

static void
call_write_as_reader(rwlock_call_state_t *state)
{
	RWLOCK_READ_ENTER(&state->lock);
	write_value(state);
	RWLOCK_EXIT(&state->lock);
}

static int
call_read_as_writer(rwlock_call_state_t *state)
{
	int value;

	RWLOCK_WRITE_ENTER(&state->lock);
	value = read_value(state);
	RWLOCK_EXIT(&state->lock);
	return (value);
}

static void
call_write_as_writer(rwlock_call_state_t *state)
{
	RWLOCK_WRITE_ENTER(&state->lock);
	write_value(state);
	RWLOCK_EXIT(&state->lock);
}

static void
acquire_reader(rwlock_call_state_t *state)
{
#ifdef __lock_lint
	_NOTE(READ_LOCK_ACQUIRED_AS_SIDE_EFFECT(state->lock))
#endif
	RWLOCK_READ_ENTER(&state->lock);
}

static void
acquire_writer(rwlock_call_state_t *state)
{
#ifdef __lock_lint
	_NOTE(WRITE_LOCK_ACQUIRED_AS_SIDE_EFFECT(state->lock))
#endif
	RWLOCK_WRITE_ENTER(&state->lock);
}

static void
release_lock(rwlock_call_state_t *state)
{
#ifdef __lock_lint
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(state->lock))
#endif
	RWLOCK_EXIT(&state->lock);
}

static int
check_reader_effect(rwlock_call_state_t *state)
{
	int value;

	acquire_reader(state);
	value = state->value;
	state->value = 1;
	release_lock(state);
	return (value);
}

static void
check_writer_effect(rwlock_call_state_t *state)
{
	acquire_writer(state);
	state->value = 1;
	release_lock(state);
}
