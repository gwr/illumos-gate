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
 * Verify that a structure-valued mutex has whole-object lock identity.
 */

#define	_NOTE(arg)

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct struct_lock_state {
	mutex_t lock;
	int value;
} struct_lock_state_t;

static mutex_t global_lock;
static int global_value;

_NOTE(MUTEX_PROTECTS_DATA(global_lock, global_value))
_NOTE(MUTEX_PROTECTS_DATA(struct_lock_state::lock,
    struct_lock_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
check_global_struct_lock(void)
{
	global_value = 1;
	mutex_enter(&global_lock);
	global_value = 2;
	mutex_exit(&global_lock);
}

static void
check_member_struct_lock(struct_lock_state_t *state)
{
	state->value = 1;
	mutex_enter(&state->lock);
	state->value = 2;
	mutex_exit(&state->lock);
}
