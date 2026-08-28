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

#define	_NOTE(arg)

typedef int mutex_t;

struct forced_state {
	mutex_t lock;
	int value;
};

extern struct forced_state forced_object;
static struct forced_state forced_static_object;

_NOTE(MUTEX_PROTECTS_DATA(forced_object.lock, forced_object.value))
_NOTE(MUTEX_PROTECTS_DATA(forced_static_object.lock,
    forced_static_object.value))
