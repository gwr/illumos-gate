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
 * Verify composition of queue-pair service entry with one module-wide outer
 * perimeter.  Controls distinguish a wrong pair-inner perimeter from a
 * missing outer perimeter.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

typedef int (*streams_qpair_outer_srvp_t)(queue_t *);

struct streams_qpair_service_outer_shadow {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int data;
};

extern krwlock_t locklint_outer;

_NOTE(RWLOCK_PROTECTS_DATA(streams_qpair_service_outer_shadow::perimeter,
    streams_qpair_service_outer_shadow::data))

static int streams_qpair_service_outer_data;
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, streams_qpair_service_outer_data))

#define	STREAMS_QPAIR_FROM_READ(q) \
	((struct streams_qpair_service_outer_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_service_outer_shadow, \
	    read_queue)))
#define	STREAMS_QPAIR_FROM_WRITE(q) \
	((struct streams_qpair_service_outer_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_service_outer_shadow, \
	    write_queue)))

extern void locklint_qpair_service(queue_t *, int,
    streams_qpair_outer_srvp_t);

static struct streams_qpair_service_outer_shadow
    streams_qpair_service_outer_first;
static struct streams_qpair_service_outer_shadow
    streams_qpair_service_outer_second;

int
streams_qpair_service_outer_read(queue_t *q)
{
	struct streams_qpair_service_outer_shadow *pair =
	    STREAMS_QPAIR_FROM_READ(q);

	pair->data++;
	return (streams_qpair_service_outer_data);
}

int
streams_qpair_service_outer_write(queue_t *q)
{
	struct streams_qpair_service_outer_shadow *pair =
	    STREAMS_QPAIR_FROM_WRITE(q);

	pair->data++;
	return (streams_qpair_service_outer_data);
}

int
streams_qpair_service_outer_other_put(queue_t *q, mblk_t *mp)
{
	return (0);
}

int
streams_qpair_service_outer_other_service(queue_t *q)
{
	return (0);
}

int
streams_qpair_service_outer_open(queue_t *q, dev_t *devp, int oflag,
    int sflag, cred_t *credp)
{
	return (0);
}

int
streams_qpair_service_outer_close(queue_t *q, int flag, cred_t *credp)
{
	return (0);
}

void
streams_qpair_service_outer_root(void)
{
	locklint_qpair_service(&streams_qpair_service_outer_first.read_queue, 0,
	    streams_qpair_service_outer_read);
	locklint_qpair_service(&streams_qpair_service_outer_first.write_queue, 1,
	    streams_qpair_service_outer_write);
	locklint_qpair_service(&streams_qpair_service_outer_second.read_queue, 0,
	    streams_qpair_service_outer_read);
	locklint_qpair_service(
	    &streams_qpair_service_outer_second.write_queue, 1,
	    streams_qpair_service_outer_write);

	(void) streams_qpair_service_outer_read(
	    &streams_qpair_service_outer_first.read_queue);
	(void) streams_qpair_service_outer_write(
	    &streams_qpair_service_outer_first.write_queue);

	rw_enter(&locklint_outer, RW_READER);
	rw_enter(&streams_qpair_service_outer_first.perimeter, RW_WRITER);
	(void) streams_qpair_service_outer_write(
	    &streams_qpair_service_outer_second.write_queue);
	rw_exit(&streams_qpair_service_outer_first.perimeter);
	rw_exit(&locklint_outer);

	rw_enter(&streams_qpair_service_outer_first.perimeter, RW_WRITER);
	(void) streams_qpair_service_outer_read(
	    &streams_qpair_service_outer_first.read_queue);
	rw_exit(&streams_qpair_service_outer_first.perimeter);
}
