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
 * Verify that a lock rooted in a callee's returned object is rebound to the
 * caller's call-result identity.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

struct returned_object {
	mutex_t lock;
};

extern struct returned_object *allocate_object(void);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static struct returned_object *
create_locked_object(void)
{
	struct returned_object *object = allocate_object();

	mutex_enter(&object->lock);
	return (object);
}

static struct returned_object *
create_locked_alias(void)
{
	struct returned_object *object = allocate_object();
	void *alias = object;

	mutex_enter(&object->lock);
	return ((struct returned_object *)alias);
}

static struct returned_object *
lock_and_return_formal(struct returned_object *object)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(object->lock))
	mutex_enter(&object->lock);
	return (object);
}

static struct returned_object *
create_locked_multiple(int first)
{
	struct returned_object *object;

	if (first) {
		object = allocate_object();
		mutex_enter(&object->lock);
		return (object);
	}
	object = allocate_object();
	mutex_enter(&object->lock);
	return (object);
}

static void
release_returned_object(void)
{
	struct returned_object *object = create_locked_object();

	mutex_exit(&object->lock);
}

static void
release_returned_alias(void)
{
	struct returned_object *object = create_locked_alias();

	mutex_exit(&object->lock);
}

static void
release_returned_formal(void)
{
	struct returned_object *argument = allocate_object();
	struct returned_object *result = lock_and_return_formal(argument);

	mutex_exit(&result->lock);
	mutex_enter(&argument->lock);
	mutex_exit(&result->lock);
}

static void
release_multiple_return(int first)
{
	struct returned_object *object = create_locked_multiple(first);

	mutex_exit(&object->lock);
}
