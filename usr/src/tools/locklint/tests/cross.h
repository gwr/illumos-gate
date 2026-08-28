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
 * Header file for: cross-caller.c and cross-callee.c
 *
 * These files cover analysis across translation units.
 * cross.h:  declares the protected structure and external functions.
 * cross-caller.c calls exernal functions with/without locks
 * cross-callee.c implements those external functions.
 *
 * Locklint analyzes both C files in one invocation to verify
 * that unique external definitions are resolved, protected member
 * lock conditions and mutex effects cross file boundaries, and member
 * symbols are remapped through caller argument types.
 */

#ifndef TEST_CROSS_H
#define	TEST_CROSS_H

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct cross_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(cross_state::lock, cross_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern int cross_locked(struct cross_state *);
extern int cross_unlocked(struct cross_state *);
extern int cross_effect(struct cross_state *);
extern int cross_read(struct cross_state *);
extern void cross_acquire(struct cross_state *);

#endif /* TEST_CROSS_H */
