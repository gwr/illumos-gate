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
 * Verify single-module STREAMS service entry under inner and outer
 * perimeters.  The direct counterexample remains unprotected.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

extern krwlock_t locklint_inner;
extern krwlock_t locklint_outer;

static int service_inner_data;
static int service_outer_data;
static int service_direct_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, service_inner_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, service_outer_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, service_direct_data))

static int
streams_modeled_service(queue_t *q)
{
	service_inner_data++;
	service_outer_data++;
	return (0);
}

static struct qinit streams_modeled_service_qinit = {
	.qi_srvp = streams_modeled_service
};

void
streams_direct_service(void)
{
	service_direct_data++;
}
