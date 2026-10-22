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
 * Verify that a fixed container conversion in a callee preserves the
 * caller's owner identity.  The embedded handles have different offsets,
 * and the final call deliberately uses a handle from a different owner.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct rwlock {
	void *_opaque[1];
} rwlock_t;

typedef struct embedded_handle {
	void *opaque;
} embedded_handle_t;

typedef struct container_owner {
	rwlock_t lock;
	embedded_handle_t read_handle;
	int padding;
	embedded_handle_t write_handle;
	int value;
} container_owner_t;

typedef int (*container_callback_t)(embedded_handle_t *);

enum container_side {
	CONTAINER_READ_SIDE,
	CONTAINER_WRITE_SIDE
};

_NOTE(RWLOCK_PROTECTS_DATA(container_owner_t::lock,
    container_owner_t::value))

extern void rw_enter(rwlock_t *, int);
extern void rw_exit(rwlock_t *);

#define	RW_WRITER	1

#define	CONTAINER_OFFSETOF(type, member) \
	((unsigned long)&(((type *)0)->member))
#define	CONTAINER_FROM(pointer, type, member) \
	((type *)((char *)(pointer) - CONTAINER_OFFSETOF(type, member)))

static container_owner_t first_owner;
static container_owner_t second_owner;

static int
read_callback(embedded_handle_t *handle)
{
	container_owner_t *owner =
	    CONTAINER_FROM(handle, container_owner_t, read_handle);

	return (owner->value);
}

static int
write_callback(embedded_handle_t *handle)
{
	container_owner_t *owner =
	    CONTAINER_FROM(handle, container_owner_t, write_handle);

	return (owner->value);
}

static int
container_entry(embedded_handle_t *handle, int side,
    container_callback_t callback)
{
	container_owner_t *owner;
	int value;

	if (side == CONTAINER_READ_SIDE) {
		owner = CONTAINER_FROM(handle, container_owner_t, read_handle);
		rw_enter(&owner->lock, RW_WRITER);
		value = callback(handle);
		rw_exit(&owner->lock);
		return (value);
	}

	owner = CONTAINER_FROM(handle, container_owner_t, write_handle);
	rw_enter(&owner->lock, RW_WRITER);
	value = callback(handle);
	rw_exit(&owner->lock);
	return (value);
}

static int
write_entry_different(embedded_handle_t *locked_handle,
    embedded_handle_t *callback_handle, container_callback_t callback)
{
	container_owner_t *owner =
	    CONTAINER_FROM(locked_handle, container_owner_t, write_handle);
	int value;

	rw_enter(&owner->lock, RW_WRITER);
	value = callback(callback_handle);
	rw_exit(&owner->lock);
	return (value);
}

int
interprocedural_container_root(void)
{
	int value;

	value = container_entry(&first_owner.read_handle, CONTAINER_READ_SIDE,
	    read_callback);
	value += container_entry(&second_owner.read_handle,
	    CONTAINER_READ_SIDE, read_callback);
	value += container_entry(&first_owner.write_handle,
	    CONTAINER_WRITE_SIDE, write_callback);
	value += container_entry(&second_owner.write_handle,
	    CONTAINER_WRITE_SIDE, write_callback);
	value += write_entry_different(&first_owner.write_handle,
	    &second_owner.write_handle, write_callback);

	return (value);
}
