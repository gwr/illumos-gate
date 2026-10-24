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
 * Verify queue-pair STREAMS open and close entry for two distinct synthetic
 * pairs.  Each pair retains separate read and write queue addresses but
 * shares one perimeter.  Direct calls and a cross-pair call remain unsafe.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

typedef int (*streams_qpair_openp_t)(queue_t *, dev_t *, int, int, cred_t *);
typedef int (*streams_qpair_closep_t)(queue_t *, int, cred_t *);

struct streams_qpair_openclose_shadow {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int open_data;
	int close_data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_qpair_openclose_shadow::perimeter,
    streams_qpair_openclose_shadow::open_data))
_NOTE(RWLOCK_PROTECTS_DATA(streams_qpair_openclose_shadow::perimeter,
    streams_qpair_openclose_shadow::close_data))

#define	STREAMS_QPAIR_FROM_READ(q) \
	((struct streams_qpair_openclose_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_openclose_shadow, \
	    read_queue)))
#define	STREAMS_QPAIR_FROM_WRITE(q) \
	((struct streams_qpair_openclose_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_openclose_shadow, \
	    write_queue)))

extern void locklint_qpair_open(queue_t *, int, streams_qpair_openp_t,
    dev_t *, int, int, cred_t *);
extern void locklint_qpair_close(queue_t *, int, streams_qpair_closep_t,
    int, cred_t *);

static struct streams_qpair_openclose_shadow streams_qpair_openclose_first;
static struct streams_qpair_openclose_shadow streams_qpair_openclose_second;
static dev_t streams_qpair_openclose_device;
static cred_t *streams_qpair_openclose_credential;

int
streams_qpair_read_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	struct streams_qpair_openclose_shadow *pair =
	    STREAMS_QPAIR_FROM_READ(q);

	pair->open_data++;
	return (0);
}

int
streams_qpair_write_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	struct streams_qpair_openclose_shadow *pair =
	    STREAMS_QPAIR_FROM_WRITE(q);

	pair->open_data++;
	return (0);
}

int
streams_qpair_read_close(queue_t *q, int flag, cred_t *credp)
{
	struct streams_qpair_openclose_shadow *pair =
	    STREAMS_QPAIR_FROM_READ(q);

	pair->close_data++;
	return (0);
}

int
streams_qpair_write_close(queue_t *q, int flag, cred_t *credp)
{
	struct streams_qpair_openclose_shadow *pair =
	    STREAMS_QPAIR_FROM_WRITE(q);

	pair->close_data++;
	return (0);
}

int
streams_qpair_openclose_other_put(queue_t *q, mblk_t *mp)
{
	return (0);
}

int
streams_qpair_openclose_other_service(queue_t *q)
{
	return (0);
}

int
streams_qpair_openclose_other_open(queue_t *q, dev_t *devp, int oflag,
    int sflag, cred_t *credp)
{
	return (0);
}

int
streams_qpair_openclose_other_close(queue_t *q, int flag, cred_t *credp)
{
	return (0);
}

void
streams_qpair_openclose_root(void)
{
	locklint_qpair_open(&streams_qpair_openclose_first.read_queue, 0,
	    streams_qpair_read_open, &streams_qpair_openclose_device, 0, 0,
	    streams_qpair_openclose_credential);
	locklint_qpair_open(&streams_qpair_openclose_first.write_queue, 1,
	    streams_qpair_write_open, &streams_qpair_openclose_device, 0, 0,
	    streams_qpair_openclose_credential);
	locklint_qpair_close(&streams_qpair_openclose_first.read_queue, 0,
	    streams_qpair_read_close, 0, streams_qpair_openclose_credential);
	locklint_qpair_close(&streams_qpair_openclose_first.write_queue, 1,
	    streams_qpair_write_close, 0, streams_qpair_openclose_credential);

	locklint_qpair_open(&streams_qpair_openclose_second.read_queue, 0,
	    streams_qpair_read_open, &streams_qpair_openclose_device, 0, 0,
	    streams_qpair_openclose_credential);
	locklint_qpair_open(&streams_qpair_openclose_second.write_queue, 1,
	    streams_qpair_write_open, &streams_qpair_openclose_device, 0, 0,
	    streams_qpair_openclose_credential);
	locklint_qpair_close(&streams_qpair_openclose_second.read_queue, 0,
	    streams_qpair_read_close, 0, streams_qpair_openclose_credential);
	locklint_qpair_close(&streams_qpair_openclose_second.write_queue, 1,
	    streams_qpair_write_close, 0, streams_qpair_openclose_credential);

	(void) streams_qpair_read_open(
	    &streams_qpair_openclose_first.read_queue,
	    &streams_qpair_openclose_device, 0, 0,
	    streams_qpair_openclose_credential);
	(void) streams_qpair_write_open(
	    &streams_qpair_openclose_first.write_queue,
	    &streams_qpair_openclose_device, 0, 0,
	    streams_qpair_openclose_credential);
	(void) streams_qpair_read_close(
	    &streams_qpair_openclose_first.read_queue, 0,
	    streams_qpair_openclose_credential);
	(void) streams_qpair_write_close(
	    &streams_qpair_openclose_first.write_queue, 0,
	    streams_qpair_openclose_credential);

	rw_enter(&streams_qpair_openclose_first.perimeter, RW_WRITER);
	(void) streams_qpair_write_close(
	    &streams_qpair_openclose_second.write_queue, 0,
	    streams_qpair_openclose_credential);
	rw_exit(&streams_qpair_openclose_first.perimeter);
}
