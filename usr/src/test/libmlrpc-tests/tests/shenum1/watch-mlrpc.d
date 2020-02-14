#!/usr/sbin/dtrace -s
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
 * Copyright 2018 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * User-level dtrace for libmlrpc
 * Usage: dtrace -s watch-mlrpc.d -p `pgrep testprog`
 */

#pragma D option flowindent

self int trace;
self int mask;

/*
 * Turn on tracing at the interesting to-level entry points
 */
pid$target::main:entry,
pid$target:libmlrpc.so.2:ndr_clnt_bind:entry,
pid$target:libmlrpc.so.2:ndr_clnt_call:entry,
pid$target:libmlrpc.so.2:ndr_pipe_worker:entry
{
	self->trace++;
}

/*
 * If traced and not masked, print entry/return
 */
pid$target:srvsvc1*::entry,
pid$target:libmlrpc.so.2::entry
/self->trace > 0 && self->mask == 0/
{
	printf("\t0x%x", arg0);
	printf("\t0x%x", arg1);
	printf("\t0x%x", arg2);
	printf("\t0x%x", arg3);
	printf("\t0x%x", arg4);
	printf("\t0x%x", arg5);
}

/*
 * This function in libmlrpc prints out lots of internal state.
 * Comment it out if you don't want that noise.
 */
pid$target:libmlrpc.so.2:ndo_trace:entry
/self->trace > 0 && self->mask == 0/
{
	printf("ndo_trace: %s", copyinstr(arg0));
}

/*
 * Mask (don't print) all function calls below these functions.
 * These make many boring, repetitive function calls like...
 *
 * Also, libmlrpc has rather deep call stacks, particularly under
 * ndr_encode_decode_common(), so this stops traces below there.
 * Remove that from the mask actions to see the details.
 */
pid$target::ndr_s_wchar:entry
{
	self->mask++;
}

/*
 * Now inverses of above, unwind order.
 */
pid$target::ndr_s_wchar:return
{
	self->mask--;
}

pid$target:srvsvc1*::return,
pid$target:libmlrpc.so.2::return
/self->trace > 0 && self->mask == 0/
{
	printf("\t0x%x", arg1);
}

pid$target::main:return,
pid$target:libmlrpc.so.2:ndr_clnt_bind:return,
pid$target:libmlrpc.so.2:ndr_clnt_call:return,
pid$target:libmlrpc.so.2:ndr_pipe_worker:return
{
	self->trace--;
}
