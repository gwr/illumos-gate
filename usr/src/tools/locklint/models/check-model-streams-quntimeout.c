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
 * Separate runs supply unlocked, driver-mutex-held, and framework close
 * entry.  A protected store checks retained driver ownership; ordinary writer
 * queries check framework ownership before and after cancellation without
 * supplying missing claims.  Only the framework claim is exempt at the wait.
 * Request IDs, callback suppression, and lifetime ordering are not modeled.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

#ifndef QUNTIMEOUT_HOLD_DRIVER_LOCK
#define	QUNTIMEOUT_HOLD_DRIVER_LOCK	0
#endif

static timeout_id_t quntimeout_id;
static kmutex_t quntimeout_driver_lock;
static int quntimeout_after_data;

_NOTE(MUTEX_PROTECTS_DATA(quntimeout_driver_lock, quntimeout_after_data))

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
extern krwlock_t locklint_inner;
static kmutex_t quntimeout_probe_lock;
static int quntimeout_claim_failure;
_NOTE(MUTEX_PROTECTS_DATA(quntimeout_probe_lock, quntimeout_claim_failure))
#endif

/* Missing framework ownership must fail independently of the driver lock. */
void
check_quntimeout(queue_t *q)
{
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	if (!rw_write_held(&locklint_inner))
		quntimeout_claim_failure = 1;
#endif
#if QUNTIMEOUT_HOLD_DRIVER_LOCK
	mutex_enter(&quntimeout_driver_lock);
#endif
	(void) quntimeout(q, quntimeout_id);
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	if (!rw_write_held(&locklint_inner))
		quntimeout_claim_failure = 1;
#endif
#if QUNTIMEOUT_HOLD_DRIVER_LOCK
	quntimeout_after_data = 1;
	mutex_exit(&quntimeout_driver_lock);
#endif
}

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
static int
streams_quntimeout_close(queue_t *q, int flag, cred_t *credp)
{
	check_quntimeout(q);
	return (0);
}

static struct qinit streams_quntimeout_qinit = {
	.qi_qclose = streams_quntimeout_close
};
#endif
