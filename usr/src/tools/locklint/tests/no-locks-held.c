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
 * Characterize ASSERT(NO_LOCKS_HELD) for definite, conditional, multiple,
 * and caller-held lock states.
 */

#define	NO_LOCKS_HELD	1

#ifndef NO_LOCKS_HELD_VARIANT
#define	NO_LOCKS_HELD_VARIANT	1
#endif

#ifdef __lock_lint
#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#else
#define	ASSERT(expr)

typedef struct kmutex {
	int opaque;
} kmutex_t;

typedef struct krwlock {
	int opaque;
} krwlock_t;

#define	RW_READER	0
#define	RW_WRITER	1

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void rw_enter(krwlock_t *, int);
extern void rw_exit(krwlock_t *);
#endif

#if NO_LOCKS_HELD_VARIANT == 9
#include <assert.h>
#endif

static kmutex_t first_mutex;
static kmutex_t second_mutex;
static krwlock_t rwlock;

#if NO_LOCKS_HELD_VARIANT == 1

void
no_locks_held_empty(void)
{
	ASSERT(NO_LOCKS_HELD);
}

#elif NO_LOCKS_HELD_VARIANT == 2

void
no_locks_held_released(void)
{
	mutex_enter(&first_mutex);
	mutex_exit(&first_mutex);
	ASSERT(NO_LOCKS_HELD);
}

#elif NO_LOCKS_HELD_VARIANT == 3

void
no_locks_held_mutex(void)
{
	mutex_enter(&first_mutex);
	ASSERT(NO_LOCKS_HELD);
	mutex_exit(&first_mutex);
}

#elif NO_LOCKS_HELD_VARIANT == 4

void
no_locks_held_reader(void)
{
	rw_enter(&rwlock, RW_READER);
	ASSERT(NO_LOCKS_HELD);
	rw_exit(&rwlock);
}

#elif NO_LOCKS_HELD_VARIANT == 5

void
no_locks_held_writer(void)
{
	rw_enter(&rwlock, RW_WRITER);
	ASSERT(NO_LOCKS_HELD);
	rw_exit(&rwlock);
}

#elif NO_LOCKS_HELD_VARIANT == 6

static void
no_locks_held_conditional_callee(void)
{
	ASSERT(NO_LOCKS_HELD);
}

void
no_locks_held_conditional_unlocked(void)
{
	no_locks_held_conditional_callee();
}

void
no_locks_held_conditional_locked(void)
{
	mutex_enter(&first_mutex);
	no_locks_held_conditional_callee();
	mutex_exit(&first_mutex);
}

#elif NO_LOCKS_HELD_VARIANT == 7

void
no_locks_held_multiple(void)
{
	mutex_enter(&first_mutex);
	mutex_enter(&second_mutex);
	ASSERT(NO_LOCKS_HELD);
	mutex_exit(&second_mutex);
	mutex_exit(&first_mutex);
}

#elif NO_LOCKS_HELD_VARIANT == 8

static void
no_locks_held_callee(void)
{
	ASSERT(NO_LOCKS_HELD);
}

void
no_locks_held_caller(void)
{
	mutex_enter(&first_mutex);
	no_locks_held_callee();
	mutex_exit(&first_mutex);
}

#elif NO_LOCKS_HELD_VARIANT == 9

void
no_locks_held_user_assert(void)
{
	mutex_enter(&first_mutex);
	assert(NO_LOCKS_HELD);
	mutex_exit(&first_mutex);
}

#else
#error "unknown NO_LOCKS_HELD_VARIANT"
#endif
