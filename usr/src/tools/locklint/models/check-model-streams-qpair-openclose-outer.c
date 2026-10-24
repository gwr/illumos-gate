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
 * Verify queue-pair open and close entry composed with one module-wide outer
 * perimeter.  Callback accesses distinguish inner reader and writer modes
 * from outer reader and writer modes.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

typedef int (*streams_qpair_outer_openp_t)(queue_t *, dev_t *, int, int,
    cred_t *);
typedef int (*streams_qpair_outer_closep_t)(queue_t *, int, cred_t *);

struct streams_qpair_openclose_outer_shadow {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int data;
};

extern krwlock_t locklint_outer;

_NOTE(RWLOCK_PROTECTS_DATA(streams_qpair_openclose_outer_shadow::perimeter,
    streams_qpair_openclose_outer_shadow::data))

static int streams_qpair_openclose_outer_read_data;
static int streams_qpair_openclose_outer_write_data;
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer,
    streams_qpair_openclose_outer_read_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer,
    streams_qpair_openclose_outer_write_data))

#define	STREAMS_QPAIR_FROM_READ(q) \
	((struct streams_qpair_openclose_outer_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_openclose_outer_shadow, \
	    read_queue)))
#define	STREAMS_QPAIR_FROM_WRITE(q) \
	((struct streams_qpair_openclose_outer_shadow *)((char *)(q) - \
	    __builtin_offsetof(struct streams_qpair_openclose_outer_shadow, \
	    write_queue)))

extern void locklint_qpair_open(queue_t *, int,
    streams_qpair_outer_openp_t, dev_t *, int, int, cred_t *);
extern void locklint_qpair_close(queue_t *, int,
    streams_qpair_outer_closep_t, int, cred_t *);

static struct streams_qpair_openclose_outer_shadow
    streams_qpair_openclose_outer_first;
static struct streams_qpair_openclose_outer_shadow
    streams_qpair_openclose_outer_second;
static dev_t streams_qpair_openclose_outer_device;
static cred_t *streams_qpair_openclose_outer_credential;

#define	STREAMS_QPAIR_OUTER_ACCESS(pair) \
	((pair)->data++, streams_qpair_openclose_outer_write_data++, \
	streams_qpair_openclose_outer_read_data)

int
streams_qpair_outer_read_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	struct streams_qpair_openclose_outer_shadow *pair =
	    STREAMS_QPAIR_FROM_READ(q);

	return (STREAMS_QPAIR_OUTER_ACCESS(pair));
}

int
streams_qpair_outer_write_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	struct streams_qpair_openclose_outer_shadow *pair =
	    STREAMS_QPAIR_FROM_WRITE(q);

	return (STREAMS_QPAIR_OUTER_ACCESS(pair));
}

int
streams_qpair_outer_read_close(queue_t *q, int flag, cred_t *credp)
{
	struct streams_qpair_openclose_outer_shadow *pair =
	    STREAMS_QPAIR_FROM_READ(q);

	return (STREAMS_QPAIR_OUTER_ACCESS(pair));
}

int
streams_qpair_outer_write_close(queue_t *q, int flag, cred_t *credp)
{
	struct streams_qpair_openclose_outer_shadow *pair =
	    STREAMS_QPAIR_FROM_WRITE(q);

	return (STREAMS_QPAIR_OUTER_ACCESS(pair));
}

int
streams_qpair_openclose_outer_other_put(queue_t *q, mblk_t *mp)
{
	return (0);
}

int
streams_qpair_openclose_outer_other_service(queue_t *q)
{
	return (0);
}

int
streams_qpair_openclose_outer_other_open(queue_t *q, dev_t *devp, int oflag,
    int sflag, cred_t *credp)
{
	return (0);
}

int
streams_qpair_openclose_outer_other_close(queue_t *q, int flag, cred_t *credp)
{
	return (0);
}

void
streams_qpair_openclose_outer_root(void)
{
	locklint_qpair_open(
	    &streams_qpair_openclose_outer_first.read_queue, 0,
	    streams_qpair_outer_read_open,
	    &streams_qpair_openclose_outer_device, 0, 0,
	    streams_qpair_openclose_outer_credential);
	locklint_qpair_open(
	    &streams_qpair_openclose_outer_first.write_queue, 1,
	    streams_qpair_outer_write_open,
	    &streams_qpair_openclose_outer_device, 0, 0,
	    streams_qpair_openclose_outer_credential);
	locklint_qpair_close(
	    &streams_qpair_openclose_outer_first.read_queue, 0,
	    streams_qpair_outer_read_close, 0,
	    streams_qpair_openclose_outer_credential);
	locklint_qpair_close(
	    &streams_qpair_openclose_outer_first.write_queue, 1,
	    streams_qpair_outer_write_close, 0,
	    streams_qpair_openclose_outer_credential);

	locklint_qpair_open(
	    &streams_qpair_openclose_outer_second.read_queue, 0,
	    streams_qpair_outer_read_open,
	    &streams_qpair_openclose_outer_device, 0, 0,
	    streams_qpair_openclose_outer_credential);
	locklint_qpair_open(
	    &streams_qpair_openclose_outer_second.write_queue, 1,
	    streams_qpair_outer_write_open,
	    &streams_qpair_openclose_outer_device, 0, 0,
	    streams_qpair_openclose_outer_credential);
	locklint_qpair_close(
	    &streams_qpair_openclose_outer_second.read_queue, 0,
	    streams_qpair_outer_read_close, 0,
	    streams_qpair_openclose_outer_credential);
	locklint_qpair_close(
	    &streams_qpair_openclose_outer_second.write_queue, 1,
	    streams_qpair_outer_write_close, 0,
	    streams_qpair_openclose_outer_credential);

	(void) streams_qpair_outer_read_open(
	    &streams_qpair_openclose_outer_first.read_queue,
	    &streams_qpair_openclose_outer_device, 0, 0,
	    streams_qpair_openclose_outer_credential);
	(void) streams_qpair_outer_write_open(
	    &streams_qpair_openclose_outer_first.write_queue,
	    &streams_qpair_openclose_outer_device, 0, 0,
	    streams_qpair_openclose_outer_credential);
	(void) streams_qpair_outer_read_close(
	    &streams_qpair_openclose_outer_first.read_queue, 0,
	    streams_qpair_openclose_outer_credential);
	(void) streams_qpair_outer_write_close(
	    &streams_qpair_openclose_outer_first.write_queue, 0,
	    streams_qpair_openclose_outer_credential);

	rw_enter(&locklint_outer, RW_WRITER);
	rw_enter(&streams_qpair_openclose_outer_first.perimeter, RW_WRITER);
	(void) streams_qpair_outer_write_close(
	    &streams_qpair_openclose_outer_second.write_queue, 0,
	    streams_qpair_openclose_outer_credential);
	rw_exit(&streams_qpair_openclose_outer_first.perimeter);
	rw_exit(&locklint_outer);

	rw_enter(&streams_qpair_openclose_outer_first.perimeter, RW_WRITER);
	(void) streams_qpair_outer_read_open(
	    &streams_qpair_openclose_outer_first.read_queue,
	    &streams_qpair_openclose_outer_device, 0, 0,
	    streams_qpair_openclose_outer_credential);
	rw_exit(&streams_qpair_openclose_outer_first.perimeter);
}
