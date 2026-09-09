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
 * Copyright 2026 Gordon W. Ross
 */

#ifdef __lock_lint
#include <sys/note.h>
#include <synch.h>
#else
#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;
#endif

struct cycle_order_state {
	mutex_t first;
	mutex_t second;
};

_NOTE(LOCK_ORDER(cycle_order_state::first cycle_order_state::second))
_NOTE(LOCK_ORDER(cycle_order_state::second cycle_order_state::first))
