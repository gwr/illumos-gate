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
 * Verify that outbound putnext delivery acquires no selected-module
 * perimeter, while preserving locks held by its caller.  Explicit roots
 * isolate the unlocked and locked paths from independent callback entry.
 * Each run selects one valid framework perimeter plus ordinary driver
 * rwlock and mutex controls, rather than combining incompatible scopes.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

static krwlock_t streams_putnext_rwlock;
static kmutex_t streams_putnext_mutex;
static int streams_putnext_perimeter_data;
static int streams_putnext_rwlock_data;
static int streams_putnext_mutex_data;

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
extern krwlock_t locklint_inner;
#define	STREAMS_PUTNEXT_PERIM	locklint_inner
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, streams_putnext_perimeter_data))
#else
extern krwlock_t locklint_outer;
#define	STREAMS_PUTNEXT_PERIM	locklint_outer
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, streams_putnext_perimeter_data))
#endif

_NOTE(RWLOCK_PROTECTS_DATA(streams_putnext_rwlock, streams_putnext_rwlock_data))
_NOTE(MUTEX_PROTECTS_DATA(streams_putnext_mutex, streams_putnext_mutex_data))

static int
streams_putnext_target(queue_t *q, mblk_t *mp)
{
	streams_putnext_perimeter_data = 1;
	streams_putnext_rwlock_data = 1;
	streams_putnext_mutex_data = 1;
	return (0);
}

static struct qinit streams_putnext_qinit = {
	.qi_putp = streams_putnext_target
};
static queue_t streams_putnext_next = {
	.q_qinfo = &streams_putnext_qinit
};
static queue_t streams_putnext_queue = {
	.q_next = &streams_putnext_next
};
static mblk_t streams_putnext_message;

void
streams_putnext_unlocked(void)
{
	putnext(&streams_putnext_queue, &streams_putnext_message);
}

void
streams_putnext_locked(void)
{
	rw_enter(&STREAMS_PUTNEXT_PERIM, RW_WRITER);
	rw_enter(&streams_putnext_rwlock, RW_WRITER);
	mutex_enter(&streams_putnext_mutex);
	putnext(&streams_putnext_queue, &streams_putnext_message);
	streams_putnext_perimeter_data = 1;
	streams_putnext_rwlock_data = 1;
	streams_putnext_mutex_data = 1;
	mutex_exit(&streams_putnext_mutex);
	rw_exit(&streams_putnext_rwlock);
	rw_exit(&STREAMS_PUTNEXT_PERIM);
}
