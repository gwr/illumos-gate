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

/*
 * Verify that LOCK_ORDER accepts the historical whitespace-separated form,
 * conventional comma separators, and a mixture of the two.  The four
 * distinct lock roles make the resolved order visible without requiring
 * acquisition analysis.
 */

#define	_NOTE(arg)

typedef int mutex_t;

struct order_annotation_state {
	mutex_t first;
	mutex_t second;
	mutex_t third;
	mutex_t fourth;
};

_NOTE(LOCK_ORDER(order_annotation_state::first
    order_annotation_state::second))
_NOTE(LOCK_ORDER(order_annotation_state::first,
    order_annotation_state::second, order_annotation_state::third))
_NOTE(LOCK_ORDER(order_annotation_state::first,
    order_annotation_state::second order_annotation_state::third,
    order_annotation_state::fourth))
