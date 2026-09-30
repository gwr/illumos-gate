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
 * Verify that a declared local acquisition is return-relative only when the
 * returned object actually carries that lock.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

struct returned_effect_object {
	mutex_t lock;
};

#define	_NOTE(arg)

extern struct returned_effect_object *returned_effect_allocate(void);
extern void mutex_enter(mutex_t *);

static struct returned_effect_object *
return_different_object(void)
{
	struct returned_effect_object *first = returned_effect_allocate();
	struct returned_effect_object *second = returned_effect_allocate();

	mutex_enter(&first->lock);
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(first->lock))
	return (second);
}
