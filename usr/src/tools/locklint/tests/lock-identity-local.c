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

void lock_identity_local(void);

void
lock_identity_local(void)
{
	mutex_t local_lock;

	mutex_enter(&local_lock);
	mutex_exit(&local_lock);
	(void) mutex_tryenter(&local_lock);
}
