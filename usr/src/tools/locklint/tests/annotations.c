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
 * This test covers preprocessing-time `_NOTE` capture and annotation
 * resolution.  It includes an unsupported annotation that must remain
 * in raw form and a `MUTEX_PROTECTS_DATA` annotation using a generated
 * member list.
 */

#define	_NOTE(arg)
#define	_OTHER(arg)
#define	LOCK_MEMBER	lock

typedef struct event_state {
	int value;
	int count;
	int lock;
} event_state;

_OTHER(MUTEX_PROTECTS_DATA(ignored::lock, ignored::value))
_NOTE(READ_ONLY_DATA(event_state::LOCK_MEMBER))
_NOTE(MUTEX_PROTECTS_DATA(event_state::lock,
    event_state::{ value count }))
