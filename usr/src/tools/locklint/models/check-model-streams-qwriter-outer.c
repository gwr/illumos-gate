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
 * Verify outer qwriter execution from ordinary shared framework entry.
 * Immediate execution must restore the outer reader claim after callback
 * return.  A guarded caller-lock downgrade makes immediate and independent
 * deferred execution distinguishable without additional scheduling options.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

extern krwlock_t locklint_outer;
extern void locklint_put(void);

static int before_write;
static int callback_write;
static int after_read;
static int after_write;
static int after_exit;
static kmutex_t caller_lock;
static int caller_data;
static krwlock_t caller_rwlock;
static int caller_after;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, before_write))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, callback_write))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, after_read))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, after_write))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, after_exit))
_NOTE(MUTEX_PROTECTS_DATA(caller_lock, caller_data))
_NOTE(RWLOCK_PROTECTS_DATA(caller_rwlock, caller_after))

/* Deferred execution cannot modify the scheduling thread's rwlock claim. */
static void
outer_writer(queue_t *q, mblk_t *mp)
{
	callback_write = 1;
	caller_data = 1;
	if (rw_write_held(&caller_rwlock))
		rw_downgrade(&caller_rwlock);
}

/* Both returning paths must leave ordinary outer entry reader-held. */
static int
outer_put(queue_t *q, mblk_t *mp)
{
	int value;

	before_write = 1;
	qwriter(q, mp, outer_writer, PERIM_OUTER);
	value = after_read;
	after_write = value;
	return (value);
}

void
outer_root(void)
{
	mutex_enter(&caller_lock);
	rw_enter(&caller_rwlock, RW_WRITER);
	locklint_put();
	caller_after = 1;
	rw_exit(&caller_rwlock);
	after_exit = 1;
	mutex_exit(&caller_lock);
}
