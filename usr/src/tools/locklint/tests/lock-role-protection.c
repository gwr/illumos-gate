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
 * Verify role-based protection declared in source and by command.  Exact
 * object identities remain distinct while any held instance of the declared
 * canonical lock role protects the selected data member.
 */

#define	_NOTE(arg)

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct command_role_object {
	mutex_t lock;
	int value;
	int exact;
} command_role_object_t;

typedef struct annotation_role_object {
	mutex_t lock;
	int value;
	int exact;
} annotation_role_object_t;

_NOTE(MUTEX_PROTECTS_DATA(command_role_object_t::lock,
    command_role_object_t))
_NOTE(MUTEX_PROTECTS_DATA(annotation_role_object_t::lock,
    annotation_role_object_t))
_NOTE(LOCK_ROLE_PROTECTS_DATA(annotation_role_object_t::lock,
    annotation_role_object_t::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

int command_role_access(command_role_object_t *, command_role_object_t *);
int annotation_role_access(annotation_role_object_t *,
    annotation_role_object_t *);

int
command_role_access(command_role_object_t *data_owner,
    command_role_object_t *lock_owner)
{
	int value;

	value = data_owner->value;
	mutex_enter(&lock_owner->lock);
	value += data_owner->value;
	value += data_owner->exact;
	mutex_exit(&lock_owner->lock);

	return (value);
}

int
annotation_role_access(annotation_role_object_t *data_owner,
    annotation_role_object_t *lock_owner)
{
	int value;

	value = data_owner->value;
	mutex_enter(&lock_owner->lock);
	value += data_owner->value;
	value += data_owner->exact;
	mutex_exit(&lock_owner->lock);

	return (value);
}

typedef struct rwlock {
	void *_opaque[1];
} rwlock_t;

typedef struct rwlock_role_object {
	rwlock_t lock;
	int value;
} rwlock_role_object_t;

_NOTE(LOCK_ROLE_PROTECTS_DATA(rwlock_role_object_t::lock,
    rwlock_role_object_t::value))

extern int rw_rdlock(rwlock_t *);
extern int rw_wrlock(rwlock_t *);
extern int rw_unlock(rwlock_t *);

int
rwlock_role_read(rwlock_role_object_t *data_owner,
    rwlock_role_object_t *lock_owner)
{
	int value;

	(void) rw_rdlock(&lock_owner->lock);
	value = data_owner->value;
	(void) rw_unlock(&lock_owner->lock);

	return (value);
}

void
rwlock_role_reader_write(rwlock_role_object_t *data_owner,
    rwlock_role_object_t *lock_owner)
{
	(void) rw_rdlock(&lock_owner->lock);
	data_owner->value = 1;
	(void) rw_unlock(&lock_owner->lock);
}

void
rwlock_role_writer_write(rwlock_role_object_t *data_owner,
    rwlock_role_object_t *lock_owner)
{
	(void) rw_wrlock(&lock_owner->lock);
	data_owner->value = 1;
	(void) rw_unlock(&lock_owner->lock);
}
