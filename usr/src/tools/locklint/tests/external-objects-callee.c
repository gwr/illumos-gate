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

#include "external-objects.h"

struct external_state external_object;
mutex_t external_global_lock;
struct external_lock_holder external_lock_holder;
struct external_state redeclared_file_state;
mutex_t redeclared_file_lock;

/* Same-named external definitions must remain distinct from caller locals. */
extern struct external_state shadowed_object;
struct external_state shadowed_object;
extern struct external_state hidden_object;
struct external_state hidden_object;
extern struct external_state block_file_object;
struct external_state block_file_object;

_NOTE(MUTEX_PROTECTS_DATA(external_object.lock,
    external_object.value))
_NOTE(MUTEX_PROTECTS_DATA(external_global_lock,
    external_data::global_value))
_NOTE(MUTEX_PROTECTS_DATA(external_lock_holder.lock,
    external_data::member_value))
_NOTE(MUTEX_PROTECTS_DATA(redeclared_file_state.lock,
    redeclared_file_state.value))
_NOTE(MUTEX_PROTECTS_DATA(redeclared_file_lock,
    external_data::redeclared_value))
_NOTE(MUTEX_PROTECTS_DATA(external_global_lock,
    external_data::function_value))
_NOTE(MUTEX_PROTECTS_DATA(shadowed_object.lock,
    shadowed_object.value))
_NOTE(MUTEX_PROTECTS_DATA(hidden_object.lock,
    hidden_object.value))
_NOTE(MUTEX_PROTECTS_DATA(external_global_lock,
    external_data::hidden_function_value))
_NOTE(MUTEX_PROTECTS_DATA(block_file_object.lock,
    block_file_object.value))
_NOTE(MUTEX_PROTECTS_DATA(external_global_lock,
    external_data::block_function_value))

static struct external_state file_state;

_NOTE(MUTEX_PROTECTS_DATA(file_state.lock, file_state.value))

int
read_global_value(struct external_data *data)
{
	return (data->global_value);
}

int
read_member_value(struct external_data *data)
{
	return (data->member_value);
}

int
read_redeclared_value(struct external_data *data)
{
	return (data->redeclared_value);
}

extern int hidden_function(struct external_data *);
extern int block_file_function(struct external_data *);

int
redeclared_file_function(struct external_data *data)
{
	return (data->function_value);
}

int
hidden_function(struct external_data *data)
{
	return (data->hidden_function_value);
}

int
block_file_function(struct external_data *data)
{
	return (data->block_function_value);
}

static int
read_callee_shadowed_object(void)
{
	return (shadowed_object.value);
}

static int
read_callee_hidden_object(void)
{
	return (hidden_object.value);
}

static int
read_callee_block_file_object(void)
{
	return (block_file_object.value);
}

static int
read_callee_redeclared_state(void)
{
	return (redeclared_file_state.value);
}

static int
read_callee_file_state(void)
{
	return (file_state.value);
}
