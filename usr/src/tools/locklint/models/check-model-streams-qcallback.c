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
 * Verify qtimeout() and qbufcall() callback entry under each selected
 * STREAMS perimeter.  Callback arguments retain their exact queue-relative
 * owner, while a scheduling-thread mutex must not enter the callback.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

extern krwlock_t locklint_inner;
extern krwlock_t locklint_outer;

static kmutex_t streams_qcallback_scheduling_lock;
static int streams_qcallback_scheduling_data;

_NOTE(MUTEX_PROTECTS_DATA(streams_qcallback_scheduling_lock,
    streams_qcallback_scheduling_data))

#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
static int streams_qcallback_outer_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, streams_qcallback_outer_data))
#endif

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
struct streams_qcallback_owner {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_qcallback_owner::perimeter,
    streams_qcallback_owner::data))

static struct streams_qcallback_owner streams_qcallback_owner;

#if QCALLBACK_QPAIR_SIDE == 0
#define	STREAMS_QCALLBACK_QUEUE	(&streams_qcallback_owner.read_queue)
#else
#define	STREAMS_QCALLBACK_QUEUE	(&streams_qcallback_owner.write_queue)
#endif
#define	STREAMS_QCALLBACK_ARGUMENT	(&streams_qcallback_owner)

#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
struct streams_qcallback_owner {
	krwlock_t perimeter;
	int data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_qcallback_owner::perimeter,
    streams_qcallback_owner::data))

static struct streams_qcallback_owner streams_qcallback_owner;

#define	STREAMS_QCALLBACK_QUEUE \
	((queue_t *)(void *)&streams_qcallback_owner)
#define	STREAMS_QCALLBACK_ARGUMENT	(&streams_qcallback_owner)

#elif (MODEL_FLAGS & D_MTOUTPERIM) != 0
static queue_t streams_qcallback_queue;

#define	STREAMS_QCALLBACK_QUEUE		(&streams_qcallback_queue)
#define	STREAMS_QCALLBACK_ARGUMENT	(&streams_qcallback_outer_data)

#else
static int streams_qcallback_inner_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, streams_qcallback_inner_data))

static queue_t streams_qcallback_queue;

#define	STREAMS_QCALLBACK_QUEUE		(&streams_qcallback_queue)
#define	STREAMS_QCALLBACK_ARGUMENT	(&streams_qcallback_inner_data)
#endif

static void
streams_qcallback(void *argument)
{
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR || \
    (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
	struct streams_qcallback_owner *owner = argument;

	owner->data++;
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	streams_qcallback_outer_data++;
#endif
#elif (MODEL_FLAGS & D_MTOUTPERIM) != 0
	(void) argument;
	streams_qcallback_outer_data++;
#else
	(void) argument;
	streams_qcallback_inner_data++;
#endif
	streams_qcallback_scheduling_data++;
}

void
streams_qcallback_root(void)
{
	mutex_enter(&streams_qcallback_scheduling_lock);
	(void) qtimeout(STREAMS_QCALLBACK_QUEUE, streams_qcallback,
	    STREAMS_QCALLBACK_ARGUMENT, 1);
	(void) qbufcall(STREAMS_QCALLBACK_QUEUE, 1, BPRI_MED,
	    streams_qcallback, STREAMS_QCALLBACK_ARGUMENT);
	mutex_exit(&streams_qcallback_scheduling_lock);
}
