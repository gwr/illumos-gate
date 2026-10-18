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
 * Model single-module STREAMS framework entry for locklint analysis.
 *
 * MODEL_PREFIX names this model instance.  MODEL_FLAGS selects the module's
 * STREAMS concurrency contract.  Conservative defaults provide a usable
 * single-module model with no implied perimeter.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

#ifndef MODEL_PREFIX
#define	MODEL_PREFIX	locklint_
#endif

#ifndef MODEL_FLAGS
#define	MODEL_FLAGS	D_MP
#endif

#define	MODEL_CAT_(left, right)	left##right
#define	MODEL_CAT(left, right)	MODEL_CAT_(left, right)
#define	MODEL_NAME(name)	MODEL_CAT(MODEL_PREFIX, name)

/*
 * A per-queue perimeter has the queue address as its model-only lock
 * identity.  The cast is analyzed but never executed; it avoids a runtime
 * instance map while preserving aliases and distinguishing different queues.
 */
#define	MODEL_PERQ_LOCK(q)	((krwlock_t *)(void *)(q))

krwlock_t MODEL_NAME(inner);
krwlock_t MODEL_NAME(outer);
static struct qinit *MODEL_NAME(qinit);
static queue_t MODEL_NAME(queue);
static mblk_t MODEL_NAME(message);
static dev_t MODEL_NAME(device);
static cred_t *MODEL_NAME(credential);
static volatile int MODEL_NAME(call_direct);
static kmutex_t MODEL_NAME(qprocsoff_mutex);
static kcondvar_t MODEL_NAME(qprocsoff_cv);

/*
 * Enabling queue processing has no caller-visible lock or blocking effect.
 * Queue-instance lifecycle state is outside this single-module model.
 */
void
qprocson(queue_t *q)
{
}

/*
 * Disabling queue processing can wait for active put and service procedures
 * to drain.  A private wait preserves every lock retained by the caller
 * without exposing STREAMS implementation locks as part of the model.  The
 * scoped exemptions permit only the selected framework perimeters across
 * that wait; unrelated driver locks remain diagnostic.
 */
void
qprocsoff(queue_t *q)
{
	mutex_enter(&MODEL_NAME(qprocsoff_mutex));
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(MODEL_NAME(outer)))
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	_NOTE(HELD_DURING_WAIT_EXEMPT_ON(MODEL_NAME(inner)))
#endif
	cv_wait(&MODEL_NAME(qprocsoff_cv), &MODEL_NAME(qprocsoff_mutex));
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(MODEL_NAME(inner)))
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(MODEL_NAME(outer)))
#endif
	mutex_exit(&MODEL_NAME(qprocsoff_mutex));
}

/*
 * Model qwriter's possible immediate callback under the selected exclusive
 * perimeter.  Per-target entry assertions separately seed the same state for
 * callbacks which run after qwriter returns.
 */
void
qwriter(queue_t *q, mblk_t *mp, void (*func)(), int perimeter)
{
	if (MODEL_NAME(call_direct)) {
		if (perimeter == PERIM_INNER) {
			rw_enter(&MODEL_NAME(inner), RW_WRITER);
			(*func)(q, mp);
			rw_exit(&MODEL_NAME(inner));
		} else {
			rw_enter(&MODEL_NAME(outer), RW_WRITER);
			(*func)(q, mp);
			rw_exit(&MODEL_NAME(outer));
		}
	}
}

/*
 * Enter the selected downstream module's put procedure.  Both framework put
 * entry and immediate putnext dispatch use this sequence so their perimeter
 * ordering and modes remain identical.
 */
static void
MODEL_NAME(put_entry)(struct qinit *qinit, queue_t *q, mblk_t *mp)
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
#if (MODEL_FLAGS & D_MTPUTSHARED) != 0
	rw_enter(MODEL_PERQ_LOCK(q), RW_READER);
#else
	rw_enter(MODEL_PERQ_LOCK(q), RW_WRITER);
#endif
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & D_MTPUTSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*qinit->qi_putp)(q, mp);
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
	rw_exit(MODEL_PERQ_LOCK(q));
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	rw_exit(&MODEL_NAME(inner));
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}

void
MODEL_NAME(put)(void)
{
	MODEL_NAME(put_entry)(MODEL_NAME(qinit), &MODEL_NAME(queue),
	    &MODEL_NAME(message));
}

/*
 * Model putnext's possible immediate downstream put procedure.  Registered
 * qi_putp procedures retain independent entry contexts for later delivery.
 */
void
putnext(queue_t *q, mblk_t *mp)
{
	queue_t *next = q->q_next;

	if (MODEL_NAME(call_direct))
		MODEL_NAME(put_entry)(next->q_qinfo, next, mp);
}

/*
 * Model framework service-procedure entry using the module's selected inner
 * mode and shared outer-perimeter entry.
 */
void
MODEL_NAME(service)(void)
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & _D_MTSVCSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*MODEL_NAME(qinit)->qi_srvp)(&MODEL_NAME(queue));
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	rw_exit(&MODEL_NAME(inner));
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}

/*
 * Model framework open-procedure entry using the open/close-specific inner
 * and outer modes.
 */
void
MODEL_NAME(open)(void)
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
#if (MODEL_FLAGS & D_MTOCEXCL) != 0
	rw_enter(&MODEL_NAME(outer), RW_WRITER);
#else
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*MODEL_NAME(qinit)->qi_qopen)(&MODEL_NAME(queue),
	    &MODEL_NAME(device), 0, 0, MODEL_NAME(credential));
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	rw_exit(&MODEL_NAME(inner));
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}

/*
 * Model framework close-procedure entry using the open/close-specific inner
 * and outer modes.
 */
void
MODEL_NAME(close)(void)
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
#if (MODEL_FLAGS & D_MTOCEXCL) != 0
	rw_enter(&MODEL_NAME(outer), RW_WRITER);
#else
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*MODEL_NAME(qinit)->qi_qclose)(&MODEL_NAME(queue), 0,
	    MODEL_NAME(credential));
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	rw_exit(&MODEL_NAME(inner));
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}
