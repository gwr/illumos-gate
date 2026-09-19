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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Characterize the canonical identity of an automatic local lock.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern int mutex_tryenter(mutex_t *);

static volatile int local_helper_value;

static int
local_helper(void)
{
	return (local_helper_value);
}

static void
acquire_helper(mutex_t *lock)
{
	mutex_enter(lock);
}

static void
release_helper(mutex_t *lock)
{
	mutex_exit(lock);
}

static void
local_lock_helper(void)
{
	mutex_t local_lock;

	mutex_enter(&local_lock);
}

static void
local_maybe_lock_helper(int take_lock)
{
	mutex_t local_lock;

	if (take_lock)
		mutex_enter(&local_lock);
}

void lock_identity_local(void);

void
lock_identity_local(void)
{
	mutex_t local_lock;

	acquire_helper(&local_lock);
	(void) local_helper();
	release_helper(&local_lock);
	(void) local_helper();
	local_lock_helper();
	local_maybe_lock_helper(local_helper_value);
	(void) local_helper();
	mutex_exit(&local_lock);
	mutex_enter(&local_lock);
	mutex_enter(&local_lock);
	mutex_exit(&local_lock);
	(void) mutex_tryenter(&local_lock);
}
