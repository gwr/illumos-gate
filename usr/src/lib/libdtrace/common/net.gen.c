/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file and it's companion *.d.in are processed by make to
 * create a *.d library file.  The *.d.in file is a template that
 * needs substitutions.  This file creates the dictionary of what
 * names may be substituted.  See Makefile.com for details.
 */

#include <sys/socket.h>

/* This used to be processed by "sed". */
#define	SED_REPLACE(x)	XYZZY_BEGIN #x = x XYZZY_END

SED_REPLACE(AF_INET)
SED_REPLACE(AF_INET6)
