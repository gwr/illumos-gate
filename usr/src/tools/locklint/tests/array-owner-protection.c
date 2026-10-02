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
 * Verify that a mutex in an aggregate protects indexed inline storage owned
 * by that same aggregate.  Dynamic subscripts within one owner must not
 * obscure the owner, while different aggregate instances and different
 * elements of an aggregate array must remain distinct.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct array_item {
	int padding, value;
} array_item_t;

typedef struct array_owner {
	mutex_t lock;
	int values[4];
	array_item_t items[4];
} array_owner_t;

_NOTE(MUTEX_PROTECTS_DATA(array_owner::lock, array_owner::values))
_NOTE(MUTEX_PROTECTS_DATA(array_owner::lock, array_owner::items))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
same_owner_scalar(array_owner_t *owner, unsigned int index)
{
	int value;

	mutex_enter(&owner->lock);
	value = owner->values[index];
	mutex_exit(&owner->lock);
	return (value);
}

static int
same_owner_nested(array_owner_t *owner, unsigned int index)
{
	int value;

	mutex_enter(&owner->lock);
	value = owner->items[index].value;
	mutex_exit(&owner->lock);
	return (value);
}

static int
different_owner(array_owner_t *locked, array_owner_t *accessed,
    unsigned int index)
{
	int value;

	mutex_enter(&locked->lock);
	value = accessed->values[index];
	mutex_exit(&locked->lock);
	return (value);
}

static int
different_owner_element(array_owner_t *owners, unsigned int locked_index,
    unsigned int accessed_index, unsigned int value_index)
{
	int value;

	mutex_enter(&owners[locked_index].lock);
	value = owners[accessed_index].items[value_index].value;
	mutex_exit(&owners[locked_index].lock);
	return (value);
}
