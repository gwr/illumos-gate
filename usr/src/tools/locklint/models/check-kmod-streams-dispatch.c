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
 * Verify that the shared STREAMS representatives preserve both possible
 * immediate callback execution and independent deferred entry.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

static kmutex_t qwriter_effect_lock;
static kmutex_t putnext_effect_lock;

static void
streams_qwriter_callback(queue_t *q, mblk_t *mp)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(qwriter_effect_lock))
	mutex_enter(&qwriter_effect_lock);
}

static int
streams_putnext_callback(queue_t *q, mblk_t *mp)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(putnext_effect_lock))
	mutex_enter(&putnext_effect_lock);
	return (0);
}

static struct qinit streams_dispatch_qinit = {
	.qi_putp = streams_putnext_callback
};

static queue_t streams_dispatch_next = {
	.q_qinfo = &streams_dispatch_qinit
};

static queue_t streams_dispatch_source = {
	.q_next = &streams_dispatch_next
};

static mblk_t streams_dispatch_message;

void
check_qwriter_dispatch(void)
{
	qwriter(&streams_dispatch_source, &streams_dispatch_message,
	    streams_qwriter_callback, PERIM_INNER);
	mutex_exit(&qwriter_effect_lock);
}

void
check_putnext_dispatch(void)
{
	putnext(&streams_dispatch_source, &streams_dispatch_message);
	mutex_exit(&putnext_effect_lock);
}
