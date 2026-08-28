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

static struct external_state file_state;

/* These extern declarations inherit the preceding internal linkage. */
static struct external_state redeclared_file_state;
extern struct external_state redeclared_file_state;

static mutex_t redeclared_file_lock;
extern mutex_t redeclared_file_lock;

/* Exercise local hiding and block-scope extern visibility separately. */
static struct external_state shadowed_object;
static struct external_state hidden_object;
static struct external_state block_file_object;

/* This file-scope extern still names the static function. */
static int
redeclared_file_function(struct external_data *data)
{
	(void) data;
	return (0);
}

extern int redeclared_file_function(struct external_data *);

static int
hidden_function(struct external_data *data)
{
	(void) data;
	return (0);
}

static int
block_file_function(struct external_data *data)
{
	(void) data;
	return (0);
}

static int
read_external_locked(void)
{
	int value;

	mutex_enter(&external_object.lock);
	value = external_object.value;
	mutex_exit(&external_object.lock);
	return (value);
}

static int
read_external_unlocked(void)
{
	return (external_object.value);
}

static int
call_global_locked(struct external_data *data)
{
	int value;

	mutex_enter(&external_global_lock);
	value = read_global_value(data);
	mutex_exit(&external_global_lock);
	return (value);
}

static int
call_global_unlocked(struct external_data *data)
{
	return (read_global_value(data));
}

static int
call_member_locked(struct external_data *data)
{
	int value;

	mutex_enter(&external_lock_holder.lock);
	value = read_member_value(data);
	mutex_exit(&external_lock_holder.lock);
	return (value);
}

static int
call_member_unlocked(struct external_data *data)
{
	return (read_member_value(data));
}

static int
call_redeclared_locked(struct external_data *data)
{
	int value;

	mutex_enter(&redeclared_file_lock);
	value = read_redeclared_value(data);
	mutex_exit(&redeclared_file_lock);
	return (value);
}

static int
read_caller_file_state(void)
{
	return (file_state.value);
}

static int
read_caller_redeclared_state(void)
{
	return (redeclared_file_state.value);
}

static int
call_redeclared_file_function(struct external_data *data)
{
	return (redeclared_file_function(data));
}

static int
read_local_shadow(void)
{
	struct external_state shadowed_object;

	return (shadowed_object.value);
}

static int
read_hidden_extern(void)
{
	struct external_state hidden_object;

	{
		/* The local hides the file-static object from this extern. */
		extern struct external_state hidden_object;

		return (hidden_object.value);
	}
}

static int
call_hidden_extern(struct external_data *data)
{
	int hidden_function;

	(void) hidden_function;
	{
		/* The local hides the file-static function from this extern. */
		extern int hidden_function(struct external_data *);

		return (hidden_function(data));
	}
}

static int
read_visible_block_extern(void)
{
	/* With no intervening declaration, the file-static object is visible. */
	extern struct external_state block_file_object;

	return (block_file_object.value);
}

static int
call_visible_block_extern(struct external_data *data)
{
	/* With no intervening declaration, the static function is visible. */
	extern int block_file_function(struct external_data *);

	return (block_file_function(data));
}
