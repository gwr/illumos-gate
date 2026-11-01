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
 * Verify that qprocson() enables fresh asynchronous put and service entry
 * for one exact queue pair.  The opening thread's mutex must not protect
 * asynchronous callback data, while the pair perimeter must protect each
 * read- and write-side callback.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

#ifndef LIFECYCLE_ENABLE
#define	LIFECYCLE_ENABLE	1
#endif

struct streams_lifecycle_pair {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_lifecycle_pair::perimeter,
    streams_lifecycle_pair::data))

static kmutex_t streams_lifecycle_scheduling_lock;
static int streams_lifecycle_async_data;

_NOTE(MUTEX_PROTECTS_DATA(streams_lifecycle_scheduling_lock,
    streams_lifecycle_async_data))

#define	STREAMS_LIFECYCLE_FROM_READ(q) \
	((struct streams_lifecycle_pair *)((char *)(q) - \
	    __builtin_offsetof(struct streams_lifecycle_pair, read_queue)))
#define	STREAMS_LIFECYCLE_FROM_WRITE(q) \
	((struct streams_lifecycle_pair *)((char *)(q) - \
	    __builtin_offsetof(struct streams_lifecycle_pair, write_queue)))

int
streams_lifecycle_read_put(queue_t *q, mblk_t *mp)
{
	struct streams_lifecycle_pair *pair = STREAMS_LIFECYCLE_FROM_READ(q);

	pair->data++;
	streams_lifecycle_async_data++;
	return (0);
}

int
streams_lifecycle_write_put(queue_t *q, mblk_t *mp)
{
	struct streams_lifecycle_pair *pair = STREAMS_LIFECYCLE_FROM_WRITE(q);

	pair->data++;
	return (0);
}

int
streams_lifecycle_read_service(queue_t *q)
{
	struct streams_lifecycle_pair *pair = STREAMS_LIFECYCLE_FROM_READ(q);

	pair->data++;
	return (0);
}

int
streams_lifecycle_write_service(queue_t *q)
{
	struct streams_lifecycle_pair *pair = STREAMS_LIFECYCLE_FROM_WRITE(q);

	pair->data++;
	return (0);
}

int
streams_lifecycle_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	mutex_enter(&streams_lifecycle_scheduling_lock);
#if LIFECYCLE_ENABLE
	qprocson(q);
#endif
	mutex_exit(&streams_lifecycle_scheduling_lock);
	return (0);
}

int
streams_lifecycle_close(queue_t *q, int flag, cred_t *credp)
{
	qprocsoff(q);
	return (0);
}
