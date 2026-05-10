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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file and it's companion *.d.in are processed by make to
 * create a *.d library file.  The *.d.in file is a template that
 * needs substitutions.  This file creates the dictionary of what
 * names may be substituted.  See Makefile.com for details.
 */

#include <inet/tcp.h>
#include <sys/netstack.h>

/* This used to be processed by "sed". */
#define	SED_REPLACE(x)	XYZZY_BEGIN #x = x XYZZY_END

SED_REPLACE(TH_FIN)
SED_REPLACE(TH_SYN)
SED_REPLACE(TH_RST)
SED_REPLACE(TH_PUSH)
SED_REPLACE(TH_ACK)
SED_REPLACE(TH_URG)
SED_REPLACE(TH_ECE)
SED_REPLACE(TH_CWR)

SED_REPLACE(TCPS_CLOSED)
SED_REPLACE(TCPS_IDLE)
SED_REPLACE(TCPS_BOUND)
SED_REPLACE(TCPS_LISTEN)
SED_REPLACE(TCPS_SYN_SENT)
SED_REPLACE(TCPS_SYN_RCVD)
SED_REPLACE(TCPS_ESTABLISHED)
SED_REPLACE(TCPS_CLOSE_WAIT)
SED_REPLACE(TCPS_FIN_WAIT_1)
SED_REPLACE(TCPS_CLOSING)
SED_REPLACE(TCPS_LAST_ACK)
SED_REPLACE(TCPS_FIN_WAIT_2)
SED_REPLACE(TCPS_TIME_WAIT)

SED_REPLACE(TCP_MIN_HEADER_LENGTH)
