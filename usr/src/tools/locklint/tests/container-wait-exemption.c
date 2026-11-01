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
 * Verify that a held lock recovered at a fixed negative container offset
 * remains definite through an indirect callback and a nested wait helper.
 */

#ifdef __lock_lint
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/note.h>
#else
#define	_NOTE(arg)

typedef struct kmutex {
	int opaque;
} kmutex_t;

typedef struct kcondvar {
	int opaque;
} kcondvar_t;
#endif

typedef struct container_handle {
	void *opaque;
} container_handle_t;

typedef struct container_owner {
	kmutex_t perimeter;
	container_handle_t queue;
} container_owner_t;

struct container_ops {
	void (*closep)(container_handle_t *);
};

struct invalid_container_ops {
	void (*closep)(container_handle_t *);
};

extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void cv_wait(kcondvar_t *, kmutex_t *);

#define	CONTAINER_OFFSETOF(type, member) \
	((unsigned long)&(((type *)0)->member))
#define	CONTAINER_FROM(pointer, type, member) \
	((type *)((char *)(pointer) - CONTAINER_OFFSETOF(type, member)))

static container_owner_t container_instance;
static struct container_ops container_operations;
static struct invalid_container_ops invalid_container_operations;
static kmutex_t container_wait_lock;
static kcondvar_t container_wait_cv;

static void
container_wait(kmutex_t *perimeter)
{
	mutex_enter(&container_wait_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(*perimeter))
	cv_wait(&container_wait_cv, &container_wait_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(*perimeter))
	mutex_exit(&container_wait_lock);
}

static void
invalid_container_wait(kmutex_t *perimeter)
{
	mutex_enter(&container_wait_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(*perimeter))
	cv_wait(&container_wait_cv, &container_wait_lock);
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(*perimeter))
	mutex_exit(&container_wait_lock);
}

static void
container_close(container_handle_t *queue)
{
	container_owner_t *owner =
	    CONTAINER_FROM(queue, container_owner_t, queue);

	container_wait(&owner->perimeter);
}

static void
invalid_container_close(container_handle_t *queue)
{
	container_owner_t *owner =
	    CONTAINER_FROM(queue, container_owner_t, queue);

	invalid_container_wait(&owner->perimeter);
}

static void
container_close_entry(container_handle_t *queue)
{
	container_owner_t *owner =
	    CONTAINER_FROM(queue, container_owner_t, queue);

	mutex_enter(&owner->perimeter);
	(*container_operations.closep)(queue);
	mutex_exit(&owner->perimeter);
}

void
container_wait_root(void)
{
	container_close_entry(&container_instance.queue);
	(*invalid_container_operations.closep)(&container_instance.queue);
}
