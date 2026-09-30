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
 * Characterize NULL and discarded call results at a returned-lock boundary.
 */

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

#define	NULL	((void *)0)

struct returned_object {
	mutex_t lock;
};

extern struct returned_object *allocate_object(void);
extern void consume_object(struct returned_object *);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static struct returned_object *
create_locked_or_null(int create)
{
	struct returned_object *object;

	if (!create)
		return (NULL);
	object = allocate_object();
	mutex_enter(&object->lock);
	return (object);
}

static struct returned_object *
return_null(void)
{
	return (NULL);
}

static void
release_nonnull_result(int create)
{
	struct returned_object *object = create_locked_or_null(create);

	if (object == NULL)
		return;
	mutex_exit(&object->lock);
}

static void
check_direct_null(void)
{
	struct returned_object *object = return_null();

	if (object != NULL)
		mutex_exit(&object->lock);
}

static void
discard_locked_result(void)
{
	(void) create_locked_or_null(1);
}

static void
consume_unchecked_result(int create)
{
	struct returned_object *object = create_locked_or_null(create);

	consume_object(object);
}
