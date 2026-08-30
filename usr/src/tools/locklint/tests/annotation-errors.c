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
 * Copyright 2026 RackTop Systems Inc.
 */

/*
 * Test diagnostics for recognized annotations whose names cannot be
 * resolved.
 */

#define	_NOTE(arg)

typedef struct error_state {
	int lock;
	int value;
} error_state_t;

static error_state_t error_object;

_NOTE(MUTEX_PROTECTS_DATA(missing_lock, error_object.value))
_NOTE(MUTEX_PROTECTS_DATA(error_object.missing_lock, error_object.value))
_NOTE(MUTEX_PROTECTS_DATA(error_state::lock, error_state::missing_value))
_NOTE(MUTEX_PROTECTS_DATA(error_state, error_object.value))
_NOTE(SCHEME_PROTECTS_DATA(not_quoted, error_object.value))
_NOTE(SCHEME_PROTECTS_DATA("scheme", error_state::missing_scheme))
_NOTE(DATA_READABLE_WITHOUT_LOCK(error_state::missing_readable))
_NOTE(READ_ONLY_DATA(error_state::missing_read_only))
