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
 * Verify qwriter callback entry under its selected module perimeter.
 * A nonconstant selector must conservatively retain both perimeter paths.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

extern krwlock_t locklint_inner;
extern krwlock_t locklint_outer;

static int inner_data;
static int outer_data;
static int forward_data;
static int unknown_data;
static kmutex_t scheduling_lock;
static int scheduling_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, inner_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, outer_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, forward_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, unknown_data))
_NOTE(MUTEX_PROTECTS_DATA(scheduling_lock, scheduling_data))

static void
streams_qwriter_inner(queue_t *q, mblk_t *mp)
{
	inner_data++;
	scheduling_data = 1;
}

static void
streams_qwriter_outer(queue_t *q, mblk_t *mp)
{
	outer_data++;
	scheduling_data = 1;
}

static void
streams_qwriter_forward(queue_t *q, mblk_t *mp)
{
	forward_data++;
	scheduling_data = 1;
}

static void
streams_qwriter_unknown(queue_t *q, mblk_t *mp)
{
	unknown_data++;
	scheduling_data = 1;
}

static queue_t streams_qwriter_queue;
static mblk_t streams_qwriter_message;

void
streams_qwriter_forwarding(void (*func)(), int perimeter)
{
	qwriter(&streams_qwriter_queue, &streams_qwriter_message, func,
	    perimeter);
}

void
check_qwriter_inner(void)
{
	mutex_enter(&scheduling_lock);
	qwriter(&streams_qwriter_queue, &streams_qwriter_message,
	    streams_qwriter_inner, PERIM_INNER);
	mutex_exit(&scheduling_lock);
}

void
check_qwriter_outer(void)
{
	mutex_enter(&scheduling_lock);
	qwriter(&streams_qwriter_queue, &streams_qwriter_message,
	    streams_qwriter_outer, PERIM_OUTER);
	mutex_exit(&scheduling_lock);
}

void
check_qwriter_forward(void)
{
	mutex_enter(&scheduling_lock);
	streams_qwriter_forwarding(streams_qwriter_forward, PERIM_INNER);
	mutex_exit(&scheduling_lock);
}

void
check_qwriter_unknown(int perimeter)
{
	mutex_enter(&scheduling_lock);
	qwriter(&streams_qwriter_queue, &streams_qwriter_message,
	    streams_qwriter_unknown, perimeter);
	mutex_exit(&scheduling_lock);
}

static int shared_before_data;
static int shared_callback_data;
static int shared_after_read;
static int shared_after_write;
static int shared_after_exit;
static int exclusive_callback_data;
static int exclusive_after_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, shared_before_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, shared_callback_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, shared_after_read))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, shared_after_write))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, shared_after_exit))
_NOTE(MUTEX_PROTECTS_DATA(scheduling_lock, exclusive_callback_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, exclusive_after_data))

static void
streams_qwriter_shared_callback(queue_t *q, mblk_t *mp)
{
	shared_callback_data = 1;
	scheduling_data = 1;
}

/*
 * Shared framework entry is read-held before qwriter.  Immediate execution
 * leaves it writer-held, while deferred submission leaves it read-held.
 * Both returning paths must still protect reads and permit framework exit.
 */
static int
streams_qwriter_shared_put(queue_t *q, mblk_t *mp)
{
	int value;

	shared_before_data = 1;
	qwriter(q, mp, streams_qwriter_shared_callback, PERIM_INNER);
	value = shared_after_read;
	shared_after_write = value;
	return (value);
}

extern void locklint_put(void);
extern void locklint_service(void);

static void
streams_qwriter_exclusive_callback(queue_t *q, mblk_t *mp)
{
	exclusive_callback_data = 1;
}

/* Already-exclusive service entry must neither reacquire nor defer. */
static int
streams_qwriter_exclusive_service(queue_t *q)
{
	qwriter(q, &streams_qwriter_message,
	    streams_qwriter_exclusive_callback, PERIM_INNER);
	exclusive_after_data = 1;
	return (0);
}

/* Framework exit must release the perimeter, but preserve the caller mutex. */
void
check_qwriter_shared_entry(void)
{
	mutex_enter(&scheduling_lock);
	locklint_put();
	locklint_service();
	mutex_exit(&scheduling_lock);
	shared_after_exit = 1;
}
