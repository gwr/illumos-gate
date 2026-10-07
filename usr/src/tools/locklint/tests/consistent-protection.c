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
 * Characterize how Old Solaris Lock Lint records and reports locks held
 * consistently across otherwise unannotated data accesses.  Separately
 * compiled variants isolate unlocked, consistently locked, inconsistently
 * locked, read-only, readers-writer, and noncompeting cases.
 */

#ifdef __lock_lint
#include <sys/mutex.h>
#include <sys/note.h>
#include <sys/rwlock.h>
#else
typedef struct kmutex {
	int opaque;
} kmutex_t;

typedef struct krwlock {
	int opaque;
} krwlock_t;

#define	RW_READER	0
#define	RW_WRITER	1
#define	_NOTE(arg)
#endif

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void rw_enter(krwlock_t *, int);
extern void rw_exit(krwlock_t *);

static kmutex_t common_lock;
static kmutex_t first_lock;
static kmutex_t second_lock;
static krwlock_t common_rwlock;
static int datum;

#ifndef CONSISTENT_PROTECTION_VARIANT
#define	CONSISTENT_PROTECTION_VARIANT	0
#endif

#if CONSISTENT_PROTECTION_VARIANT == 1

int
protection_all_unlocked(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = datum;
	datum = value + 1;
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}

#elif CONSISTENT_PROTECTION_VARIANT == 2

int
protection_one_mutex(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	mutex_enter(&common_lock);
	value = datum;
	datum = value + 1;
	mutex_exit(&common_lock);
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}

#elif CONSISTENT_PROTECTION_VARIANT == 3

int
protection_mixed_mutex(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	mutex_enter(&common_lock);
	value = datum;
	mutex_exit(&common_lock);
	datum = value + 1;
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}

#elif CONSISTENT_PROTECTION_VARIANT == 4

int
protection_two_mutexes(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	mutex_enter(&first_lock);
	value = datum;
	mutex_exit(&first_lock);
	mutex_enter(&second_lock);
	datum = value + 1;
	mutex_exit(&second_lock);
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}

#elif CONSISTENT_PROTECTION_VARIANT == 5

int
protection_common_plus_extra(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	mutex_enter(&common_lock);
	mutex_enter(&first_lock);
	value = datum;
	mutex_exit(&first_lock);
	mutex_exit(&common_lock);

	mutex_enter(&common_lock);
	mutex_enter(&second_lock);
	datum = value + 1;
	mutex_exit(&second_lock);
	mutex_exit(&common_lock);
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}

#elif CONSISTENT_PROTECTION_VARIANT == 6

int
protection_reads_unlocked(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = datum;
	value += datum;
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}

#elif CONSISTENT_PROTECTION_VARIANT == 7

int
protection_rwlock_modes(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	rw_enter(&common_rwlock, RW_READER);
	value = datum;
	rw_exit(&common_rwlock);
	rw_enter(&common_rwlock, RW_WRITER);
	datum = value + 1;
	rw_exit(&common_rwlock);
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}

#elif CONSISTENT_PROTECTION_VARIANT == 8

int
protection_no_competition(void)
{
	int value;

	_NOTE(NO_COMPETING_THREADS_NOW)
	value = datum;
	datum = value + 1;
	_NOTE(COMPETING_THREADS_NOW)

	return (value);
}

#endif
