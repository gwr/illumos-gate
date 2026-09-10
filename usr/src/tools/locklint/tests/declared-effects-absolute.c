/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify validation and propagation of effects on canonical absolute locks.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;

static mutex_t absolute_mutex;
static rwlock_t absolute_rwlock;
static int absolute_value;

_NOTE(MUTEX_PROTECTS_DATA(absolute_mutex, absolute_value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void rw_rdlock(rwlock_t *);

static void
declared_absolute_acquire(void)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(absolute_mutex))
	mutex_enter(&absolute_mutex);
}

static void
declared_absolute_missing(void)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(absolute_mutex))
}

static void
declared_absolute_conditional(int acquire)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(absolute_mutex))
	if (acquire)
		mutex_enter(&absolute_mutex);
}

static void
declared_absolute_wrong_mode(void)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(absolute_rwlock))
	rw_rdlock(&absolute_rwlock);
}

static void
undeclared_absolute_acquire(void)
{
	mutex_enter(&absolute_mutex);
}

static void
check_absolute_acquire(void)
{
	declared_absolute_acquire();
	absolute_value = 1;
	mutex_exit(&absolute_mutex);
}

static void
check_absolute_already_held(void)
{
	mutex_enter(&absolute_mutex);
	declared_absolute_acquire();
	mutex_exit(&absolute_mutex);
}

static void
declared_absolute_release(void)
{
	_NOTE(LOCK_RELEASED_AS_SIDE_EFFECT(absolute_mutex))
	mutex_exit(&absolute_mutex);
}

static void
check_absolute_release(void)
{
	mutex_enter(&absolute_mutex);
	declared_absolute_release();
	absolute_value = 2;
}

static void
check_absolute_release_unheld(void)
{
	declared_absolute_release();
}

static void
acquire_formal(mutex_t *lock)
{
	mutex_enter(lock);
}

static void
declared_absolute_formal_wrapper(void)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(absolute_mutex))
	acquire_formal(&absolute_mutex);
}

static void
check_absolute_formal_wrapper(void)
{
	declared_absolute_formal_wrapper();
	absolute_value = 3;
	mutex_exit(&absolute_mutex);
}
