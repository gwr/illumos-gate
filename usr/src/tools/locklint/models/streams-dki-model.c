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
 * single-module model with no implied perimeter.  An analysis runner may
 * define MODEL_QWRITER_QPAIR_SIDE as zero or one when every modeled qwriter
 * call is known to use that queue side.
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

#if defined(MODEL_QWRITER_QPAIR_SIDE) && \
    MODEL_QWRITER_QPAIR_SIDE != 0 && MODEL_QWRITER_QPAIR_SIDE != 1
#error "MODEL_QWRITER_QPAIR_SIDE must be zero or one"
#endif
#if defined(MODEL_QWRITER_QPAIR_SIDE) && \
    (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) != D_MTQPAIR
#error "MODEL_QWRITER_QPAIR_SIDE requires D_MTQPAIR"
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

struct MODEL_NAME(qpair) {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
};

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

static void MODEL_NAME(qpair_qwriter_invoke)(queue_t *, int, mblk_t *,
    void (*)(queue_t *, mblk_t *));

/*
 * Invoke a PERIM_INNER callback for D_MTQPAIR.  The exact side derives the
 * pair-relative writer perimeter while preserving the concrete queue passed
 * to the callback.
 */
static void
MODEL_NAME(qpair_qwriter_invoke)(queue_t *q, int side, mblk_t *mp,
    void (*func)(queue_t *, mblk_t *))
{
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
	if (side == 0) {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), read_queue));

		rw_enter(&pair->perimeter, RW_WRITER);
		(*func)(q, mp);
		rw_exit(&pair->perimeter);
	} else {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), write_queue));

		rw_enter(&pair->perimeter, RW_WRITER);
		(*func)(q, mp);
		rw_exit(&pair->perimeter);
	}
#else
	(*func)(q, mp);
#endif
}

/*
 * Invoke qwriter under the selected exclusive perimeter.  A configured
 * queue-pair side maps inner entry to the pair containing the concrete queue;
 * otherwise inner entry uses the module-wide synthetic perimeter.
 */
static void
MODEL_NAME(qwriter_invoke)(queue_t *q, mblk_t *mp, void (*func)(),
    int perimeter)
{
	if (perimeter == PERIM_INNER) {
#ifdef MODEL_QWRITER_QPAIR_SIDE
		MODEL_NAME(qpair_qwriter_invoke)(q,
		    MODEL_QWRITER_QPAIR_SIDE, mp,
		    (void (*)(queue_t *, mblk_t *))func);
#else
		rw_enter(&MODEL_NAME(inner), RW_WRITER);
		(*func)(q, mp);
		rw_exit(&MODEL_NAME(inner));
#endif
	} else {
		rw_enter(&MODEL_NAME(outer), RW_WRITER);
		(*func)(q, mp);
		rw_exit(&MODEL_NAME(outer));
	}
}

/*
 * Deferred qwriter entry retains exact arguments but not locks held by the
 * scheduling thread.
 */
static void
MODEL_NAME(qwriter_deferred)(queue_t *q, mblk_t *mp, void (*func)(),
    int perimeter)
{
	MODEL_NAME(qwriter_invoke)(q, mp, func, perimeter);
}

_NOTE(ASYNC_ENTRY(MODEL_NAME(qwriter_deferred)))

/*
 * Model qwriter's possible immediate and deferred callback execution.
 */
void
qwriter(queue_t *q, mblk_t *mp, void (*func)(), int perimeter)
{
	if (MODEL_NAME(call_direct))
		MODEL_NAME(qwriter_invoke)(q, mp, func, perimeter);
	MODEL_NAME(qwriter_deferred)(q, mp, func, perimeter);
}

/*
 * Deferred qwriter execution retains the call's queue, side, message, and
 * callback bindings but starts without locks held by the scheduling thread.
 */
static void
MODEL_NAME(qpair_qwriter_deferred)(queue_t *q, int side, mblk_t *mp,
    void (*func)(queue_t *, mblk_t *))
{
	MODEL_NAME(qpair_qwriter_invoke)(q, side, mp, func);
}

_NOTE(ASYNC_ENTRY(MODEL_NAME(qpair_qwriter_deferred)))

/*
 * Model qwriter's possible immediate and deferred PERIM_INNER callback for
 * D_MTQPAIR.
 */
void
MODEL_NAME(qpair_qwriter)(queue_t *q, int side, mblk_t *mp,
    void (*func)(queue_t *, mblk_t *))
{
	if (MODEL_NAME(call_direct))
		MODEL_NAME(qpair_qwriter_invoke)(q, side, mp, func);
	MODEL_NAME(qpair_qwriter_deferred)(q, side, mp, func);
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
 * Enter a D_MTQPAIR put procedure under the perimeter shared by the concrete
 * read and write queues, nested inside the optional module outer perimeter.
 * Branch-local fixed offsets preserve both the exact queue passed to the
 * callback and the identity of its containing pair.
 */
void
MODEL_NAME(qpair_put)(queue_t *q, int side,
    int (*putp)(queue_t *, mblk_t *), mblk_t *mp)
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
	if (side == 0) {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), read_queue));

#if (MODEL_FLAGS & D_MTPUTSHARED) != 0
		rw_enter(&pair->perimeter, RW_READER);
#else
		rw_enter(&pair->perimeter, RW_WRITER);
#endif
		(*putp)(q, mp);
		rw_exit(&pair->perimeter);
	} else {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), write_queue));

#if (MODEL_FLAGS & D_MTPUTSHARED) != 0
		rw_enter(&pair->perimeter, RW_READER);
#else
		rw_enter(&pair->perimeter, RW_WRITER);
#endif
		(*putp)(q, mp);
		rw_exit(&pair->perimeter);
	}
#else
	(*putp)(q, mp);
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
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
 * Enter a D_MTQPAIR service procedure as a writer under the perimeter shared
 * by the concrete read and write queues, nested inside the optional module
 * outer perimeter.  STREAMS does not support shared service entry for
 * queue-pair perimeters.
 */
void
MODEL_NAME(qpair_service)(queue_t *q, int side, int (*srvp)(queue_t *))
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
	if (side == 0) {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), read_queue));

		rw_enter(&pair->perimeter, RW_WRITER);
		(*srvp)(q);
		rw_exit(&pair->perimeter);
	} else {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), write_queue));

		rw_enter(&pair->perimeter, RW_WRITER);
		(*srvp)(q);
		rw_exit(&pair->perimeter);
	}
#else
	(*srvp)(q);
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}

/*
 * Model framework service-procedure entry using the module's selected inner
 * mode and shared outer-perimeter entry.
 */
void
MODEL_NAME(service)(void)
{
	queue_t *q = &MODEL_NAME(queue);

#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
	rw_enter(MODEL_PERQ_LOCK(q), RW_WRITER);
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & _D_MTSVCSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*MODEL_NAME(qinit)->qi_srvp)(q);
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
	rw_exit(MODEL_PERQ_LOCK(q));
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	rw_exit(&MODEL_NAME(inner));
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}

/*
 * Enter a D_MTQPAIR open procedure under the perimeter shared by the
 * concrete read and write queues, nested inside the optional module outer
 * perimeter.  Branch-local fixed offsets preserve the pair identity while
 * the original queue is passed to the callback.
 */
void
MODEL_NAME(qpair_open)(queue_t *q, int side,
    int (*openp)(queue_t *, dev_t *, int, int, cred_t *), dev_t *devp,
    int oflag, int sflag, cred_t *credp)
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
#if (MODEL_FLAGS & D_MTOCEXCL) != 0
	rw_enter(&MODEL_NAME(outer), RW_WRITER);
#else
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
	if (side == 0) {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), read_queue));

#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
		rw_enter(&pair->perimeter, RW_READER);
#else
		rw_enter(&pair->perimeter, RW_WRITER);
#endif
		(*openp)(q, devp, oflag, sflag, credp);
		rw_exit(&pair->perimeter);
	} else {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), write_queue));

#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
		rw_enter(&pair->perimeter, RW_READER);
#else
		rw_enter(&pair->perimeter, RW_WRITER);
#endif
		(*openp)(q, devp, oflag, sflag, credp);
		rw_exit(&pair->perimeter);
	}
#else
	(*openp)(q, devp, oflag, sflag, credp);
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}

/*
 * Enter a D_MTQPAIR close procedure under the same pair-relative perimeter
 * and inner and outer modes as open entry.
 */
void
MODEL_NAME(qpair_close)(queue_t *q, int side,
    int (*closep)(queue_t *, int, cred_t *), int flag, cred_t *credp)
{
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
#if (MODEL_FLAGS & D_MTOCEXCL) != 0
	rw_enter(&MODEL_NAME(outer), RW_WRITER);
#else
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTQPAIR
	if (side == 0) {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), read_queue));

#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
		rw_enter(&pair->perimeter, RW_READER);
#else
		rw_enter(&pair->perimeter, RW_WRITER);
#endif
		(*closep)(q, flag, credp);
		rw_exit(&pair->perimeter);
	} else {
		struct MODEL_NAME(qpair) *pair =
		    (void *)((char *)q -
		    __builtin_offsetof(struct MODEL_NAME(qpair), write_queue));

#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
		rw_enter(&pair->perimeter, RW_READER);
#else
		rw_enter(&pair->perimeter, RW_WRITER);
#endif
		(*closep)(q, flag, credp);
		rw_exit(&pair->perimeter);
	}
#else
	(*closep)(q, flag, credp);
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
	queue_t *q = &MODEL_NAME(queue);

#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
#if (MODEL_FLAGS & D_MTOCEXCL) != 0
	rw_enter(&MODEL_NAME(outer), RW_WRITER);
#else
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
	rw_enter(MODEL_PERQ_LOCK(q), RW_READER);
#else
	rw_enter(MODEL_PERQ_LOCK(q), RW_WRITER);
#endif
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*MODEL_NAME(qinit)->qi_qopen)(q, &MODEL_NAME(device), 0, 0,
	    MODEL_NAME(credential));
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
	rw_exit(MODEL_PERQ_LOCK(q));
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
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
	queue_t *q = &MODEL_NAME(queue);

#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
#if (MODEL_FLAGS & D_MTOCEXCL) != 0
	rw_enter(&MODEL_NAME(outer), RW_WRITER);
#else
	rw_enter(&MODEL_NAME(outer), RW_READER);
#endif
#endif
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
	rw_enter(MODEL_PERQ_LOCK(q), RW_READER);
#else
	rw_enter(MODEL_PERQ_LOCK(q), RW_WRITER);
#endif
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & _D_MTOCSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*MODEL_NAME(qinit)->qi_qclose)(q, 0, MODEL_NAME(credential));
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
	rw_exit(MODEL_PERQ_LOCK(q));
#elif (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	rw_exit(&MODEL_NAME(inner));
#endif
#if (MODEL_FLAGS & D_MTOUTPERIM) != 0
	rw_exit(&MODEL_NAME(outer));
#endif
}
