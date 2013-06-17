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
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * User-level dtrace for smbd
 * Usage: dtrace -s ThisScript.d -p PID
 */

pid$target:fksmbd::entry,
pid$target:libfksmbsrv.so.1::entry,
pid$target:libmlsvc.so.1::entry,
pid$target:libmlrpc.so.1::entry,
pid$target:libsmbns.so.1::entry,
pid$target:libsmb.so.1::entry
{
  printf("\t0x%x", arg0);
  printf("\t0x%x", arg1);
  printf("\t0x%x", arg2);
  printf("\t0x%x", arg3);
}

pid$target:fksmbd::return,
pid$target:libfksmbsrv.so.1::return,
pid$target:libmlsvc.so.1::return,
pid$target:libmlrpc.so.1::return,
pid$target:libsmbns.so.1::return,
pid$target:libsmb.so.1::return
{
  printf("\t0x%x", arg1);
}
