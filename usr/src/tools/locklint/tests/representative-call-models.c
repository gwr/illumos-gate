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
 * This file is input only to locklint.  It supplies the representative
 * locking behavior for representative_ops::enter and is not compiled or
 * linked into a product.
 */

#include "representative-call.h"

#define	_NOTE(arg)

extern void mutex_enter(mutex_t *);
extern struct representative_object *representative_allocate(void);

void
representative_enter(struct representative_object *object)
{
	mutex_enter(&object->lock);
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(object->lock))
}

struct representative_object *
representative_create(void)
{
	struct representative_object *object = representative_allocate();

	mutex_enter(&object->lock);
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(object->lock))
	return (object);
}
