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
 * Verify observed acquisition consistency for concrete operation targets.
 */

#include "contract-consistency.h"

extern void mutex_enter(mutex_t *);
extern struct consistency_object *consistency_allocate(void);

void
consistency_acquire(struct consistency_object *object)
{
	mutex_enter(&object->lock);
}

void
consistency_none(struct consistency_object *object)
{
	(void) object;
}

struct consistency_object *
consistency_create(void)
{
	struct consistency_object *object = consistency_allocate();

	mutex_enter(&object->lock);
	return (object);
}

#if CONTRACT_CONSISTENCY_IMPLICIT
static const struct implicit_consistency_ops implicit_consistency = {
	.enter = consistency_acquire
};

static const struct implicit_return_consistency_ops
implicit_return_consistency = {
	.create = consistency_create
};
#endif

#if CONTRACT_CONSISTENCY_EXPLICIT
static const struct explicit_consistency_ops explicit_consistency = {
	.enter = consistency_acquire
};
#endif

#if CONTRACT_CONSISTENCY_MATCHING
static const struct matching_consistency_ops matching_consistency = {
	.enter = consistency_acquire
};

static const struct matching_return_consistency_ops
matching_return_consistency = {
	.create = consistency_create
};
#endif

#if CONTRACT_CONSISTENCY_CONFLICTING
static const struct conflicting_consistency_ops conflicting_consistency = {
	.enter = consistency_none
};
#endif
