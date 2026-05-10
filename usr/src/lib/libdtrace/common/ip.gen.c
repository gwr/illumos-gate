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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file and it's companion *.d.in are processed by make to
 * create a *.d library file.  The *.d.in file is a template that
 * needs substitutions.  This file creates the dictionary of what
 * names may be substituted.  See Makefile.com for details.
 */

#include <sys/netstack.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <inet/ip.h>
#include <inet/tcp.h>

/* This used to be processed by "sed". */
#define	SED_REPLACE(x)	XYZZY_BEGIN #x = x XYZZY_END

SED_REPLACE(AF_INET)
SED_REPLACE(AF_INET6)

SED_REPLACE(IPH_DF)
SED_REPLACE(IPH_MF)

SED_REPLACE(IPPROTO_IP)
SED_REPLACE(IPPROTO_HOPOPTS)
SED_REPLACE(IPPROTO_ICMP)
SED_REPLACE(IPPROTO_IGMP)
SED_REPLACE(IPPROTO_GGP)
SED_REPLACE(IPPROTO_ENCAP)
SED_REPLACE(IPPROTO_TCP)
SED_REPLACE(IPPROTO_EGP)
SED_REPLACE(IPPROTO_PUP)
SED_REPLACE(IPPROTO_UDP)
SED_REPLACE(IPPROTO_IDP)
SED_REPLACE(IPPROTO_IPV6)
SED_REPLACE(IPPROTO_ROUTING)
SED_REPLACE(IPPROTO_FRAGMENT)
SED_REPLACE(IPPROTO_RSVP)
SED_REPLACE(IPPROTO_ESP)
SED_REPLACE(IPPROTO_AH)
SED_REPLACE(IPPROTO_ICMPV6)
SED_REPLACE(IPPROTO_NONE)
SED_REPLACE(IPPROTO_DSTOPTS)
SED_REPLACE(IPPROTO_HELLO)
SED_REPLACE(IPPROTO_ND)
SED_REPLACE(IPPROTO_EON)
SED_REPLACE(IPPROTO_OSPF)
SED_REPLACE(IPPROTO_PIM)
SED_REPLACE(IPPROTO_SCTP)
SED_REPLACE(IPPROTO_RAW)
SED_REPLACE(IPPROTO_MAX)

SED_REPLACE(TCP_MIN_HEADER_LENGTH)

SED_REPLACE(GLOBAL_NETSTACKID)
