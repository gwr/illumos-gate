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
 * entry.  A protected store checks retained driver ownership; ordinary
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

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD || \
	(MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
#define	CANCEL_INNER_LOCK(q) ((krwlock_t *)(void *)(q))
#else
extern krwlock_t locklint_inner;
#define	CANCEL_INNER_LOCK(q) (&locklint_inner)
#endif

#ifdef CANCEL_HOLD_OTHER_QUEUE
static queue_t cancel_other_queue;
#endif
static kmutex_t quntimeout_probe_lock;
static int quntimeout_claim_failure;
_NOTE(MUTEX_PROTECTS_DATA(quntimeout_probe_lock, quntimeout_claim_failure))

/* Shared close entry must remain reader-only, not silently become writer. */
#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
#define	CANCEL_INNER_HELD(q) \
	(rw_read_held(CANCEL_INNER_LOCK(q)) && \
	!rw_write_held(CANCEL_INNER_LOCK(q)))
#else
#define	CANCEL_INNER_HELD(q) rw_write_held(CANCEL_INNER_LOCK(q))
#endif
#endif

/* Missing framework ownership must fail independently of the driver lock. */
void
#if defined(CANCEL_NO_ENTRY) && !defined(CANCEL_OPAQUE_QUEUE)
check_quntimeout(void)
#else
check_quntimeout(queue_t *q)
#endif
{
#if defined(CANCEL_NO_ENTRY) && !defined(CANCEL_OPAQUE_QUEUE)
	static queue_t unlocked_queue;
	queue_t *q = &unlocked_queue;
#endif
#if !defined(CANCEL_NO_ENTRY) && \
	((MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD || \
	(MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ)
	if (!CANCEL_INNER_HELD(q))
		quntimeout_claim_failure = 1;
#endif
#ifdef CANCEL_HOLD_OTHER_QUEUE
	rw_enter(CANCEL_INNER_LOCK(&cancel_other_queue), RW_WRITER);
#endif
#if QUNTIMEOUT_HOLD_DRIVER_LOCK
	mutex_enter(&quntimeout_driver_lock);
#endif
	(void) quntimeout(q, quntimeout_id);
#if !defined(CANCEL_NO_ENTRY) && \
	((MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD || \
	(MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ)
	if (!CANCEL_INNER_HELD(q))
		quntimeout_claim_failure = 1;
#endif
#ifdef CANCEL_HOLD_OTHER_QUEUE
	if (!rw_write_held(CANCEL_INNER_LOCK(&cancel_other_queue)))
		quntimeout_claim_failure = 1;
	rw_exit(CANCEL_INNER_LOCK(&cancel_other_queue));
#endif
#if QUNTIMEOUT_HOLD_DRIVER_LOCK
	quntimeout_after_data = 1;
	mutex_exit(&quntimeout_driver_lock);
#endif
}

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD || \
	(MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
#ifndef CANCEL_NO_ENTRY
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
#endif
