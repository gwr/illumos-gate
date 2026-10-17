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
 * Characterize qprocsoff() under framework close-entry perimeters, with and
 * without an additional driver mutex.  The selected framework perimeters are
 * exempt across the modeled drain wait, while the driver mutex must remain
 * diagnostic.
 */

#include <sys/types.h>
#include <sys/stream.h>
#include <sys/synch.h>

#ifndef QPROC_HOLD_DRIVER_LOCK
#define	QPROC_HOLD_DRIVER_LOCK	0
#endif

static kmutex_t qproc_driver_lock;

static int
streams_qproc_close(queue_t *q, int flag, cred_t *credp)
{
#if QPROC_HOLD_DRIVER_LOCK
	mutex_enter(&qproc_driver_lock);
#endif
	qprocsoff(q);
#if QPROC_HOLD_DRIVER_LOCK
	mutex_exit(&qproc_driver_lock);
#endif
	return (0);
}

static struct qinit streams_qproc_qinit = {
	.qi_qclose = streams_qproc_close
};
