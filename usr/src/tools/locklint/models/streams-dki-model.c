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

krwlock_t MODEL_NAME(inner);
static struct qinit *MODEL_NAME(qinit);
static queue_t MODEL_NAME(queue);
static mblk_t MODEL_NAME(message);

void
MODEL_NAME(put)(void)
{
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
#if (MODEL_FLAGS & D_MTPUTSHARED) != 0
	rw_enter(&MODEL_NAME(inner), RW_READER);
#else
	rw_enter(&MODEL_NAME(inner), RW_WRITER);
#endif
#endif
	(*MODEL_NAME(qinit)->qi_putp)(&MODEL_NAME(queue),
	    &MODEL_NAME(message));
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
	rw_exit(&MODEL_NAME(inner));
#endif
}
