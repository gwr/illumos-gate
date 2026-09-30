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
 * Verify that an unresolved member call uses its representative contract.
 */

#include "representative-call.h"

extern void mutex_exit(mutex_t *);

void
concrete_enter(struct representative_object *object)
{
	(void) object;
}

static void
call_representative(struct representative_ops *ops,
    struct representative_object *object)
{
	ops->enter(object);
	mutex_exit(&object->lock);
}
