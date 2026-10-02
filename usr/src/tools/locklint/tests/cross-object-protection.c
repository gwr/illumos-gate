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
 * Characterize protection of a separately allocated object by a lock in
 * another object.  The cases distinguish exact owner association, recovery
 * through a pointer-valued member, and matching only the declared lock role.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct cross_object_child {
	int value;
} cross_object_child_t;

typedef struct cross_object_owner {
	mutex_t lock;
	cross_object_child_t *child;
} cross_object_owner_t;

_NOTE(MUTEX_PROTECTS_DATA(cross_object_owner_t::lock,
    cross_object_child_t))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

int cross_object_matching(cross_object_owner_t *);
int cross_object_saved_local(cross_object_owner_t *);
int cross_object_wrong_owner(cross_object_owner_t *, cross_object_owner_t *);
int cross_object_unrelated(cross_object_owner_t *, cross_object_child_t *);
int cross_object_reassigned(cross_object_owner_t *, cross_object_child_t *);

/*
 * Establish the baseline for a direct access through the pointer member.
 * The first access is unlocked; the second uses the apparent parent lock.
 */
int
cross_object_matching(cross_object_owner_t *owner)
{
	int value;

	value = owner->child->value;
	mutex_enter(&owner->lock);
	value += owner->child->value;
	mutex_exit(&owner->lock);
	return (value);
}

/*
 * Determine whether saving the member value in a local preserves whatever
 * relationship OSLL recognizes between the pointee and its owner.
 */
int
cross_object_saved_local(cross_object_owner_t *owner)
{
	cross_object_child_t *child = owner->child;
	int value;

	value = child->value;
	mutex_enter(&owner->lock);
	value += child->value;
	mutex_exit(&owner->lock);
	return (value);
}

/*
 * Hold one formal owner's lock while accessing the other formal owner's
 * pointee.  Distinct actual arguments prevent a same-expression match.
 */
int
cross_object_wrong_owner(cross_object_owner_t *data_owner,
    cross_object_owner_t *lock_owner)
{
	int value;

	value = data_owner->child->value;
	mutex_enter(&lock_owner->lock);
	value += data_owner->child->value;
	mutex_exit(&lock_owner->lock);
	return (value);
}

/*
 * Access a child supplied independently of any owner pointer member.  If the
 * held owner lock satisfies this access, OSLL is matching the lock role
 * rather than recovering an owner-to-pointee association.
 */
int
cross_object_unrelated(cross_object_owner_t *owner,
    cross_object_child_t *child)
{
	int value;

	value = child->value;
	mutex_enter(&owner->lock);
	value += child->value;
	mutex_exit(&owner->lock);
	return (value);
}

/*
 * Preserve the old pointee across replacement of the owner member.  Exact
 * current-slot association would distinguish old_child from owner->child.
 */
int
cross_object_reassigned(cross_object_owner_t *owner,
    cross_object_child_t *replacement)
{
	cross_object_child_t *old_child = owner->child;
	int value;

	owner->child = replacement;
	value = old_child->value;
	value += owner->child->value;
	mutex_enter(&owner->lock);
	value += old_child->value;
	value += owner->child->value;
	mutex_exit(&owner->lock);
	return (value);
}
