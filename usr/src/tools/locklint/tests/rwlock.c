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
 * Test local readers-writer lock policy, state, operations, and assertions.
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
	RW_READER,
	RW_READER_STARVEWRITER
} krw_t;
#else
typedef struct rwlock {
	void *_opaque[1];
} rwlock_t;
#endif
#endif

#define	ASSERT(expr)
#ifndef __lock_lint
#ifdef _KERNEL
#define	RW_READ_HELD(lock)	rw_read_held(lock)
#define	RW_WRITE_HELD(lock)	rw_write_held(lock)
#define	RW_LOCK_HELD(lock)	rw_lock_held(lock)
#else
#define	RW_READ_HELD(lock)	_rw_read_held(lock)
#define	RW_WRITE_HELD(lock)	_rw_write_held(lock)
#endif
#endif

#ifdef _KERNEL
typedef krwlock_t test_rwlock_t;
#define	RWLOCK_READ_ENTER(lock)	rw_enter((lock), RW_READER)
#define	RWLOCK_WRITE_ENTER(lock)	rw_enter((lock), RW_WRITER)
#define	RWLOCK_STARVEWRITER_ENTER(lock)	\
	rw_enter((lock), RW_READER_STARVEWRITER)
#define	RWLOCK_EXIT(lock)	rw_exit(lock)
#else
typedef rwlock_t test_rwlock_t;
#define	RWLOCK_READ_ENTER(lock)	((void) rw_rdlock(lock))
#define	RWLOCK_WRITE_ENTER(lock)	((void) rw_wrlock(lock))
#define	RWLOCK_STARVEWRITER_ENTER(lock)	RWLOCK_READ_ENTER(lock)
#define	RWLOCK_EXIT(lock)	((void) rw_unlock(lock))
#endif

typedef struct rwlock_state {
	test_rwlock_t lock;
	int value;
	int readable;
	int scheme;
} rwlock_state_t;

_NOTE(RWLOCK_PROTECTS_DATA(rwlock_state::lock, rwlock_state::value))
_NOTE(RWLOCK_PROTECTS_DATA(rwlock_state::lock, rwlock_state::readable))
_NOTE(DATA_READABLE_WITHOUT_LOCK(rwlock_state::readable))
_NOTE(SCHEME_PROTECTS_DATA("external", rwlock_state::scheme))

#ifndef __lock_lint
#ifdef _KERNEL
extern void rw_enter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
extern int rw_read_held(krwlock_t *);
extern int rw_write_held(krwlock_t *);
extern int rw_lock_held(krwlock_t *);
#else
extern int rw_rdlock(rwlock_t *);
extern int rw_wrlock(rwlock_t *);
extern int rw_unlock(rwlock_t *);
extern int _rw_read_held(void *);
extern int _rw_write_held(void *);
#endif
#endif

static int
check_modes(rwlock_state_t *state)
{
	int value;

	value = state->value;
	state->value = 1;
	value += state->readable;
	state->readable = 1;
	state->scheme = 1;

	RWLOCK_READ_ENTER(&state->lock);
	value += state->value;
	state->value = 2;
	RWLOCK_EXIT(&state->lock);

	RWLOCK_WRITE_ENTER(&state->lock);
	value += state->value;
	state->value = 3;
	RWLOCK_EXIT(&state->lock);

	return (value);
}

#if defined(_KERNEL) && !defined(LOCKLINT_RWLOCK_CORE_ONLY)
static void
check_unknown_mode(rwlock_state_t *state, krw_t mode)
{
	rw_enter(&state->lock, mode);
	state->value = 1;
}
#endif

static int
check_mode_merge(rwlock_state_t *state, int writer)
{
	int value;

	if (writer)
		RWLOCK_WRITE_ENTER(&state->lock);
	else
		RWLOCK_READ_ENTER(&state->lock);
	value = state->value;
	state->value = 1;
	RWLOCK_EXIT(&state->lock);
	return (value);
}

#ifndef LOCKLINT_RWLOCK_CORE_ONLY
static int
check_unheld_merge(rwlock_state_t *state, int take)
{
	int value;

	if (take)
		RWLOCK_STARVEWRITER_ENTER(&state->lock);
	value = state->value;
	return (value);
}

static void
check_invalid_operations(rwlock_state_t *state)
{
	RWLOCK_EXIT(&state->lock);

	RWLOCK_READ_ENTER(&state->lock);
	RWLOCK_READ_ENTER(&state->lock);
	RWLOCK_EXIT(&state->lock);

	RWLOCK_READ_ENTER(&state->lock);
	RWLOCK_WRITE_ENTER(&state->lock);
	RWLOCK_EXIT(&state->lock);
}

static int
check_read_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(RW_READ_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}

static int
check_write_assertion(rwlock_state_t *state)
{
	ASSERT(RW_WRITE_HELD(&state->lock));
	state->value = 1;
	return (state->value);
}

#ifdef _KERNEL
static int
check_lock_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(RW_LOCK_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}

static void
check_not_held_assertion(rwlock_state_t *state)
{
	RWLOCK_WRITE_ENTER(&state->lock);
	ASSERT(!RW_LOCK_HELD(&state->lock));
	RWLOCK_READ_ENTER(&state->lock);
	RWLOCK_EXIT(&state->lock);
}
#endif

static int
check_not_read_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(!RW_READ_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}

static int
check_not_write_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(!RW_WRITE_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}
#endif
