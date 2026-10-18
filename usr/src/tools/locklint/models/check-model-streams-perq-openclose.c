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
 * Verify D_MTPERQ open and close entry using the queue address as a
 * model-only synthetic rwlock identity.  Framework entry should protect the
 * selected queue, while direct callback invocations remain unprotected.  The
 * read-modify-write accesses also distinguish exclusive entry from
 * _D_MTOCSHARED reader entry.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

struct streams_perq_openclose_shadow {
	krwlock_t perimeter;
	int open_data;
	int close_data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_perq_openclose_shadow::perimeter,
    streams_perq_openclose_shadow::open_data))
_NOTE(RWLOCK_PROTECTS_DATA(streams_perq_openclose_shadow::perimeter,
    streams_perq_openclose_shadow::close_data))

static struct streams_perq_openclose_shadow
    streams_perq_openclose_direct_state;
static dev_t streams_perq_openclose_device;

int
streams_perq_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	struct streams_perq_openclose_shadow *shadow = (void *)q;

	shadow->open_data++;
	return (0);
}

int
streams_perq_close(queue_t *q, int flag, cred_t *credp)
{
	struct streams_perq_openclose_shadow *shadow = (void *)q;

	shadow->close_data++;
	return (0);
}

static int
streams_perq_openclose_put(queue_t *q, mblk_t *mp)
{
	return (0);
}

static int
streams_perq_openclose_service(queue_t *q)
{
	return (0);
}

void
streams_perq_openclose_direct(void)
{
	queue_t *q = (queue_t *)(void *)&streams_perq_openclose_direct_state;

	(void) streams_perq_open(q, &streams_perq_openclose_device, 0, 0,
	    (cred_t *)0);
	(void) streams_perq_close(q, 0, (cred_t *)0);
}
