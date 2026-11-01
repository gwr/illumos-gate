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
 * Verify immediate queue-relative qwriter inner dispatch for two distinct
 * synthetic queue pairs.  The original queue selects the callback object
 * while an exact side selects the pair-relative perimeter.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

typedef void (*streams_qpair_writer_t)(queue_t *, mblk_t *);

struct streams_qpair_qwriter_shadow {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int qwriter_data;
	int standard_qwriter_data;
	int wrong_pair_data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_qpair_qwriter_shadow::perimeter,
    streams_qpair_qwriter_shadow::{
    qwriter_data standard_qwriter_data wrong_pair_data }))

#define	STREAMS_QPAIR_FROM_READ(q) \
	((struct streams_qpair_qwriter_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_qwriter_shadow, read_queue)))
#define	STREAMS_QPAIR_FROM_WRITE(q) \
	((struct streams_qpair_qwriter_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_qwriter_shadow, \
	    write_queue)))

extern void locklint_qpair_qwriter(queue_t *, int, mblk_t *,
    streams_qpair_writer_t);

struct streams_qpair_qwriter_shadow streams_qpair_qwriter_first;
static struct streams_qpair_qwriter_shadow streams_qpair_qwriter_second;
static mblk_t streams_qpair_qwriter_message;
static kmutex_t streams_qpair_qwriter_caller_lock;
static int streams_qpair_qwriter_caller_data;

_NOTE(MUTEX_PROTECTS_DATA(streams_qpair_qwriter_caller_lock,
    streams_qpair_qwriter_caller_data))

void
streams_qpair_read_writer(queue_t *q, mblk_t *mp)
{
	struct streams_qpair_qwriter_shadow *pair = STREAMS_QPAIR_FROM_READ(q);

	pair->qwriter_data++;
	streams_qpair_qwriter_caller_data = 1;
}

void
streams_qpair_write_writer(queue_t *q, mblk_t *mp)
{
	struct streams_qpair_qwriter_shadow *pair = STREAMS_QPAIR_FROM_WRITE(q);

	pair->qwriter_data++;
	streams_qpair_qwriter_caller_data = 1;
}

void
streams_qpair_reentrant_writer(queue_t *q, mblk_t *mp)
{
	struct streams_qpair_qwriter_shadow *pair = STREAMS_QPAIR_FROM_READ(q);

	pair->qwriter_data++;
}

void
streams_qpair_wrong_pair_writer(queue_t *q, mblk_t *mp)
{
	struct streams_qpair_qwriter_shadow *pair = STREAMS_QPAIR_FROM_WRITE(q);

	pair->wrong_pair_data = 1;
}

void
streams_qpair_standard_writer(queue_t *q, mblk_t *mp)
{
	struct streams_qpair_qwriter_shadow *pair = STREAMS_QPAIR_FROM_WRITE(q);

	pair->standard_qwriter_data = 1;
	streams_qpair_qwriter_caller_data = 1;
}

int
streams_qpair_qwriter_other_put(queue_t *q, mblk_t *mp)
{
	return (0);
}

int
streams_qpair_qwriter_other_service(queue_t *q)
{
	return (0);
}

int
streams_qpair_qwriter_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	return (0);
}

int
streams_qpair_qwriter_close(queue_t *q, int flag, cred_t *credp)
{
	return (0);
}

void
streams_qpair_qwriter_root(void)
{
	mutex_enter(&streams_qpair_qwriter_caller_lock);
	locklint_qpair_qwriter(&streams_qpair_qwriter_first.read_queue, 0,
	    &streams_qpair_qwriter_message, streams_qpair_read_writer);
	locklint_qpair_qwriter(&streams_qpair_qwriter_first.write_queue, 1,
	    &streams_qpair_qwriter_message, streams_qpair_write_writer);
	locklint_qpair_qwriter(&streams_qpair_qwriter_second.read_queue, 0,
	    &streams_qpair_qwriter_message, streams_qpair_read_writer);
	locklint_qpair_qwriter(&streams_qpair_qwriter_second.write_queue, 1,
	    &streams_qpair_qwriter_message, streams_qpair_write_writer);
#ifdef MODEL_QWRITER_QPAIR_SIDE
	qwriter(&streams_qpair_qwriter_second.write_queue,
	    &streams_qpair_qwriter_message, streams_qpair_standard_writer,
	    PERIM_INNER);
#endif
	mutex_exit(&streams_qpair_qwriter_caller_lock);

	rw_enter(&streams_qpair_qwriter_first.perimeter, RW_WRITER);
	streams_qpair_wrong_pair_writer(
	    &streams_qpair_qwriter_second.write_queue,
	    &streams_qpair_qwriter_message);
	rw_exit(&streams_qpair_qwriter_first.perimeter);
}

void
streams_qpair_qwriter_reentrance_root(void)
{
#ifdef MODEL_QPAIR_REENTRANCE
	locklint_qpair_qwriter(&streams_qpair_qwriter_first.read_queue, 0,
	    &streams_qpair_qwriter_message, streams_qpair_reentrant_writer);
#endif
}
