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
 * Verify single-module STREAMS open and close entry under inner and outer
 * perimeters.  The direct counterexample remains unprotected.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

extern krwlock_t locklint_inner;
extern krwlock_t locklint_outer;

static int open_inner_data;
static int open_outer_data;
static int close_inner_data;
static int close_outer_data;
static int openclose_direct_data;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, open_inner_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, open_outer_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, close_inner_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_outer, close_outer_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, openclose_direct_data))

static int
streams_modeled_open(queue_t *q, dev_t *devp, int flag, int sflag,
    cred_t *credp)
{
	open_inner_data++;
	open_outer_data++;
	return (0);
}

static int
streams_modeled_close(queue_t *q, int flag, cred_t *credp)
{
	close_inner_data++;
	close_outer_data++;
	return (0);
}

static struct qinit streams_modeled_openclose_qinit = {
	.qi_qopen = streams_modeled_open,
	.qi_qclose = streams_modeled_close
};

void
streams_direct_openclose(void)
{
	openclose_direct_data++;
}
