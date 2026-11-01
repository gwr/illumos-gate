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
 * Verify exact read- and write-side callback selection through the model's
 * module operation vectors.  Every callback updates data protected by the
 * shared queue-pair perimeter, so a callback reached with the wrong queue
 * side also loses the expected protection.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

struct streams_module_entry_pair {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_module_entry_pair::perimeter,
    streams_module_entry_pair::data))

#define	STREAMS_MODULE_FROM_READ(q) \
	((struct streams_module_entry_pair *)((char *)(q) - \
	    __builtin_offsetof(struct streams_module_entry_pair, read_queue)))
#define	STREAMS_MODULE_FROM_WRITE(q) \
	((struct streams_module_entry_pair *)((char *)(q) - \
	    __builtin_offsetof(struct streams_module_entry_pair, write_queue)))

static void
streams_module_read_access(queue_t *q)
{
	struct streams_module_entry_pair *pair = STREAMS_MODULE_FROM_READ(q);

	pair->data++;
}

static void
streams_module_write_access(queue_t *q)
{
	struct streams_module_entry_pair *pair = STREAMS_MODULE_FROM_WRITE(q);

	pair->data++;
}

int
streams_module_read_put(queue_t *q, mblk_t *mp)
{
	streams_module_read_access(q);
	return (0);
}

int
streams_module_write_put(queue_t *q, mblk_t *mp)
{
	streams_module_write_access(q);
	return (0);
}

int
streams_module_read_service(queue_t *q)
{
	streams_module_read_access(q);
	return (0);
}

int
streams_module_write_service(queue_t *q)
{
	streams_module_write_access(q);
	return (0);
}

int
streams_module_read_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	streams_module_read_access(q);
	return (0);
}

int
streams_module_write_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	streams_module_write_access(q);
	return (0);
}

int
streams_module_read_close(queue_t *q, int flag, cred_t *credp)
{
	streams_module_read_access(q);
	return (0);
}

int
streams_module_write_close(queue_t *q, int flag, cred_t *credp)
{
	streams_module_write_access(q);
	return (0);
}
