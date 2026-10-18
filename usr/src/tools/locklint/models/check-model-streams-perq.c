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
 * Verify queue-relative STREAMS put entry using the queue_t * value as a
 * model-only synthetic rwlock identity.  Framework entry should protect the
 * instance selected by q, while a direct callback invocation remains
 * unprotected.  The read-modify-write access also distinguishes exclusive
 * entry from D_MTPUTSHARED reader entry.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

struct streams_perq_shadow {
	krwlock_t perimeter;
	int data;
};

_NOTE(RWLOCK_PROTECTS_DATA(streams_perq_shadow::perimeter,
    streams_perq_shadow::data))

static struct streams_perq_shadow streams_perq_direct_state;
static mblk_t streams_perq_direct_message;

int
streams_perq_put(queue_t *q, mblk_t *mp)
{
	struct streams_perq_shadow *shadow = (void *)q;

	shadow->data++;
	return (0);
}

static int
streams_perq_service(queue_t *q)
{
	return (0);
}

static int
streams_perq_open(queue_t *q, dev_t *devp, int oflag, int sflag,
    cred_t *credp)
{
	return (0);
}

static int
streams_perq_close(queue_t *q, int flag, cred_t *credp)
{
	return (0);
}

void
streams_perq_direct(void)
{
	(void) streams_perq_put((queue_t *)(void *)&streams_perq_direct_state,
	    &streams_perq_direct_message);
}
