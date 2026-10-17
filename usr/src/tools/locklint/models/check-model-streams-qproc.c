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
 * Verify the blocking distinction between qprocson() and qprocsoff() while
 * preserving caller lock state.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/synch.h>

static queue_t qproc_queue;
static kmutex_t qprocson_outer_lock;
static kmutex_t qprocsoff_outer_lock;

void
check_qprocson(void)
{
	mutex_enter(&qprocson_outer_lock);
	qprocson(&qproc_queue);
	mutex_exit(&qprocson_outer_lock);
}

void
check_qprocsoff(void)
{
	mutex_enter(&qprocsoff_outer_lock);
	qprocsoff(&qproc_queue);
	mutex_exit(&qprocsoff_outer_lock);
}
