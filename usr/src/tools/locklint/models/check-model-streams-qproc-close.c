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
 * diagnostic.  Combined queue-pair/outer cases check ownership on both sides
 * of the wait so missing claims cannot make the exemption test pass.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/stream.h>
#include <sys/synch.h>

#ifndef QPROC_HOLD_DRIVER_LOCK
#define	QPROC_HOLD_DRIVER_LOCK	0
#endif

static kmutex_t qproc_driver_lock;

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
/* Match the model's fixed queue-pair layout for the read-side close entry. */
struct qproc_pair {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
};

extern krwlock_t locklint_outer;

/* Require the configured mode, not just an arbitrary held outer claim. */
#if (MODEL_FLAGS & D_MTOCEXCL) != 0
#define	QPROC_OUTER_HELD()	rw_write_held(&locklint_outer)
#else
#define	QPROC_OUTER_HELD()	rw_read_held(&locklint_outer)
#endif

/*
 * Ordinary queries check ownership without seeding a missing claim.  A
 * separate diagnostic lock keeps failure visible even with the driver mutex
 * held; neither framework ownership nor that mutex protects this control.
 */
static kmutex_t qproc_probe_lock;
static int qproc_claim_failure;
_NOTE(MUTEX_PROTECTS_DATA(qproc_probe_lock, qproc_claim_failure))
#endif

/* Framework claims must survive the wait independently of the driver mutex. */
static int
streams_qproc_close(queue_t *q, int flag, cred_t *credp)
{
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
	struct qproc_pair *pair = (void *)((char *)q -
	    __builtin_offsetof(struct qproc_pair, read_queue));

	if (!rw_write_held(&pair->perimeter) || !QPROC_OUTER_HELD())
		qproc_claim_failure = 1;
#endif
#if QPROC_HOLD_DRIVER_LOCK
	mutex_enter(&qproc_driver_lock);
#endif
	qprocsoff(q);
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
	if (!rw_write_held(&pair->perimeter) || !QPROC_OUTER_HELD())
		qproc_claim_failure = 1;
#endif
#if QPROC_HOLD_DRIVER_LOCK
	mutex_exit(&qproc_driver_lock);
#endif
	return (0);
}

static struct qinit streams_qproc_qinit = {
	.qi_qclose = streams_qproc_close
};
