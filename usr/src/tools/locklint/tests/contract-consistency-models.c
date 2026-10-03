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
 * This file is input only to locklint.  It supplies the effectful contract
 * used by acquisition-consistency tests and is not linked into a product.
 */

#include "contract-consistency.h"

#define	_NOTE(arg)

extern void mutex_enter(mutex_t *);
extern struct consistency_object *consistency_allocate(void);

void
consistency_representative(struct consistency_object *object)
{
	mutex_enter(&object->lock);
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(object->lock))
}

struct consistency_object *
consistency_create_representative(void)
{
	struct consistency_object *object = consistency_allocate();

	mutex_enter(&object->lock);
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(object->lock))
	return (object);
}
