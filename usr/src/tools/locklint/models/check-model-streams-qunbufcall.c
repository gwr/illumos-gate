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
 * Verify qunbufcall's potential blocking without transferring caller locks.
 * Separate runs supply unlocked and driver-mutex-held entry.  A protected
 * store after cancellation checks that the locked caller retains ownership.
 * Request IDs, callback suppression, and lifetime ordering are not modeled.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

#ifndef QUNBUFCALL_HOLD_DRIVER_LOCK
#define	QUNBUFCALL_HOLD_DRIVER_LOCK	0
#endif

static queue_t qunbufcall_queue;
static bufcall_id_t qunbufcall_id;
static kmutex_t qunbufcall_driver_lock;
static int qunbufcall_after_data;

_NOTE(MUTEX_PROTECTS_DATA(qunbufcall_driver_lock, qunbufcall_after_data))

void
check_qunbufcall(void)
{
#if QUNBUFCALL_HOLD_DRIVER_LOCK
	mutex_enter(&qunbufcall_driver_lock);
#endif
	qunbufcall(&qunbufcall_queue, qunbufcall_id);
#if QUNBUFCALL_HOLD_DRIVER_LOCK
	qunbufcall_after_data = 1;
	mutex_exit(&qunbufcall_driver_lock);
#endif
}
