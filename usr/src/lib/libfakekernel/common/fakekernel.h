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

#ifndef _FAKEKERNEL_H
#define	_FAKEKERNEL_H

#include <stdio_tag.h>

#ifdef __cplusplus
extern "C" {
#endif

void _set_logfile(__FILE_TAG *);
void fakekernel_redirect_cmn_err(__FILE_TAG *, int);

#ifdef __cplusplus
}
#endif

#endif /* _FAKEKERNEL_H */
