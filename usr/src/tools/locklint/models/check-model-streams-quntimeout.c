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
 * Verify quntimeout's potential blocking without transferring caller locks.
 * Separate runs supply unlocked and driver-mutex-held entry.  A protected
 * store after cancellation checks that the locked caller retains ownership.
 * Request IDs, callback suppression, and lifetime ordering are not modeled.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

#ifndef QUNTIMEOUT_HOLD_DRIVER_LOCK
#define	QUNTIMEOUT_HOLD_DRIVER_LOCK	0
#endif

static queue_t quntimeout_queue;
static timeout_id_t quntimeout_id;
static kmutex_t quntimeout_driver_lock;
static int quntimeout_after_data;

_NOTE(MUTEX_PROTECTS_DATA(quntimeout_driver_lock, quntimeout_after_data))

void
check_quntimeout(void)
{
#if QUNTIMEOUT_HOLD_DRIVER_LOCK
	mutex_enter(&quntimeout_driver_lock);
#endif
	(void) quntimeout(&quntimeout_queue, quntimeout_id);
#if QUNTIMEOUT_HOLD_DRIVER_LOCK
	quntimeout_after_data = 1;
	mutex_exit(&quntimeout_driver_lock);
#endif
}
