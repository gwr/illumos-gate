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
 * Verify qwriter callback entry under its selected module perimeter.
 * A nonconstant selector must conservatively retain both perimeter paths.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

extern krwlock_t locklint_inner;
extern krwlock_t locklint_outer;

static int inner_data;
static int outer_data;
static int forward_data;
static int unknown_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, inner_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, outer_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, forward_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, unknown_data))

static void
streams_qwriter_inner(queue_t *q, mblk_t *mp)
{
	inner_data++;
}

static void
streams_qwriter_outer(queue_t *q, mblk_t *mp)
{
	outer_data++;
}

static void
streams_qwriter_forward(queue_t *q, mblk_t *mp)
{
	forward_data++;
}

static void
streams_qwriter_unknown(queue_t *q, mblk_t *mp)
{
	unknown_data++;
}

static queue_t streams_qwriter_queue;
static mblk_t streams_qwriter_message;

void
streams_qwriter_forwarding(void (*func)(), int perimeter)
{
	qwriter(&streams_qwriter_queue, &streams_qwriter_message, func,
	    perimeter);
}

void
check_qwriter_inner(void)
{
	qwriter(&streams_qwriter_queue, &streams_qwriter_message,
	    streams_qwriter_inner, PERIM_INNER);
}

void
check_qwriter_outer(void)
{
	qwriter(&streams_qwriter_queue, &streams_qwriter_message,
	    streams_qwriter_outer, PERIM_OUTER);
}

void
check_qwriter_forward(void)
{
	streams_qwriter_forwarding(streams_qwriter_forward, PERIM_INNER);
}

void
check_qwriter_unknown(int perimeter)
{
	qwriter(&streams_qwriter_queue, &streams_qwriter_message,
	    streams_qwriter_unknown, perimeter);
}
