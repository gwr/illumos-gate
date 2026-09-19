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
 * Characterize canonical identities for external and file-static locks.
 * Each lock is referenced twice to distinguish reuse from conflation.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

extern mutex_t external_lock;
static mutex_t static_lock;

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

void lock_identity_globals(void);

void
lock_identity_globals(void)
{
	mutex_enter(&external_lock);
	mutex_exit(&external_lock);
	mutex_enter(&static_lock);
	mutex_exit(&static_lock);
}
