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
 * Verify that LOCK_ORDER rejects lists that cannot describe an edge.  The
 * cases distinguish too few names from leading, repeated, and trailing
 * commas so accepting optional separators does not silently accept malformed
 * declarations.
 */

#define	_NOTE(arg)

typedef int mutex_t;

struct order_error_state {
	mutex_t first;
	mutex_t second;
};

_NOTE(LOCK_ORDER(order_error_state::first))
_NOTE(LOCK_ORDER(, order_error_state::first order_error_state::second))
_NOTE(LOCK_ORDER(order_error_state::first,, order_error_state::second))
_NOTE(LOCK_ORDER(order_error_state::first order_error_state::second,))
