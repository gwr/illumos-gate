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
 * Verify single-module STREAMS put entry with and without an inner perimeter.
 * The direct counterexample must remain unprotected in both configurations.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

extern krwlock_t locklint_inner;
extern krwlock_t locklint_outer;

static int modeled_data;
static int outer_data;
static int direct_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, modeled_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, outer_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, direct_data))

static int
streams_modeled_put(queue_t *q, mblk_t *mp)
{
	modeled_data++;
	outer_data++;
	return (0);
}

static struct qinit streams_modeled_qinit = {
	.qi_putp = streams_modeled_put
};

void
streams_direct_put(void)
{
	direct_data++;
}
