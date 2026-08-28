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
 * Verify that lock conditions preserve absolute locks through direct and
 * transitive calls.  Cover both a bare global lock and a lock member in a
 * global object.
 */

#define	_NOTE(arg)

typedef int mutex_t;

struct lock_holder {
	mutex_t lock;
};

struct global_condition_data {
	int bare;
	int member;
};

static mutex_t global_lock;
static struct lock_holder global_holder;

_NOTE(MUTEX_PROTECTS_DATA(global_lock,
    global_condition_data::bare))
_NOTE(MUTEX_PROTECTS_DATA(global_holder.lock,
    global_condition_data::member))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
read_bare(struct global_condition_data *data)
{
	return (data->bare);
}

static int
wrap_bare(struct global_condition_data *data)
{
	return (read_bare(data));
}

static int
read_member(struct global_condition_data *data)
{
	return (data->member);
}

static int
wrap_member(struct global_condition_data *data)
{
	return (read_member(data));
}

static int
locked_bare_direct(struct global_condition_data *data)
{
	int value;

	mutex_enter(&global_lock);
	value = read_bare(data);
	mutex_exit(&global_lock);
	return (value);
}

static int
unlocked_bare_direct(struct global_condition_data *data)
{
	return (read_bare(data));
}

static int
locked_bare_transitive(struct global_condition_data *data)
{
	int value;

	mutex_enter(&global_lock);
	value = wrap_bare(data);
	mutex_exit(&global_lock);
	return (value);
}

static int
unlocked_bare_transitive(struct global_condition_data *data)
{
	return (wrap_bare(data));
}

static int
locked_member_direct(struct global_condition_data *data)
{
	int value;

	mutex_enter(&global_holder.lock);
	value = read_member(data);
	mutex_exit(&global_holder.lock);
	return (value);
}

static int
unlocked_member_direct(struct global_condition_data *data)
{
	return (read_member(data));
}

static int
locked_member_transitive(struct global_condition_data *data)
{
	int value;

	mutex_enter(&global_holder.lock);
	value = wrap_member(data);
	mutex_exit(&global_holder.lock);
	return (value);
}

static int
unlocked_member_transitive(struct global_condition_data *data)
{
	return (wrap_member(data));
}
