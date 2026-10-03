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
 * Verify that merge-instances selectively replaces per-instance object and
 * member-lock identities with one representative instance for a named type.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct merge_instance_controller {
	int padding;
	mutex_t lock;
} merge_instance_controller_t;

typedef struct merge_instance_payload {
	int value;
} merge_instance_payload_t;

typedef struct merge_instance_unrelated {
	int padding;
	mutex_t other_lock;
} merge_instance_unrelated_t;

typedef int merge_instance_scalar_t;

enum merge_instance_enum {
	MERGE_INSTANCE_ENUM_VALUE
};

_NOTE(MUTEX_PROTECTS_DATA(merge_instance_controller::lock,
    merge_instance_payload))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

void merge_instance_protected(merge_instance_controller_t *,
    merge_instance_payload_t *);
void merge_instance_distinct(merge_instance_controller_t *,
    merge_instance_controller_t *);
void merge_instance_unrelated_distinct(merge_instance_unrelated_t *,
    merge_instance_unrelated_t *);

void
merge_instance_protected(merge_instance_controller_t *controller,
    merge_instance_payload_t *payload)
{
	mutex_enter(&controller->lock);
	payload->value++;
	mutex_exit(&controller->lock);
}

void
merge_instance_distinct(merge_instance_controller_t *first,
    merge_instance_controller_t *second)
{
	mutex_enter(&first->lock);
	mutex_enter(&second->lock);
	mutex_exit(&second->lock);
	mutex_exit(&first->lock);
}

void
merge_instance_unrelated_distinct(merge_instance_unrelated_t *first,
    merge_instance_unrelated_t *second)
{
	mutex_enter(&first->other_lock);
	mutex_enter(&second->other_lock);
	mutex_exit(&second->other_lock);
	mutex_exit(&first->other_lock);
}
