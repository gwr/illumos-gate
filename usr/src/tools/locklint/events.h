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

#ifndef EVENTS_H
#define	EVENTS_H

struct entrypoint;
struct instruction;
struct locklint_access;
struct translation_unit;

enum locklint_lock_action {
	LOCKLINT_LOCK_NONE,
	LOCKLINT_LOCK_ACQUIRE,
	LOCKLINT_LOCK_RELEASE,
	LOCKLINT_LOCK_WAIT
};

enum locklint_lock_mode {
	LOCKLINT_MODE_UNHELD = 1 << 0,
	LOCKLINT_MODE_MUTEX = 1 << 1,
	LOCKLINT_MODE_READER = 1 << 2,
	LOCKLINT_MODE_WRITER = 1 << 3
};

enum locklint_lock_action locklint_get_lock_action(struct translation_unit *,
    struct instruction *, struct locklint_access *, enum locklint_lock_mode *);
void locklint_show_events(struct translation_unit *, struct entrypoint *);

#endif /* EVENTS_H */
