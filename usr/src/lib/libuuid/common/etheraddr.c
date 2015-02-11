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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 */

#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <stropts.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <sys/sockio.h>
#include <sys/utsname.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "etheraddr.h"

/*
 * get an individual arp entry
 */
static int
arp_get(uuid_node_t *node)
{
	struct utsname name;
	struct arpreq ar;
	struct hostent *hp;
	struct sockaddr_in *sin;
	int s;

	if (uname(&name) == -1) {
		return (-1);
	}
	(void) memset(&ar, 0, sizeof (ar));
	ar.arp_pa.sa_family = AF_INET;
	/* LINTED pointer */
	sin = (struct sockaddr_in *)&ar.arp_pa;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = inet_addr(name.nodename);
	if (sin->sin_addr.s_addr == (in_addr_t)-1) {
		hp = gethostbyname(name.nodename);
		if (hp == NULL) {
			return (-1);
		}
		(void) memcpy(&sin->sin_addr, hp->h_addr,
		    sizeof (sin->sin_addr));
	}
	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		return (-1);
	}
	if (ioctl(s, SIOCGARP, (caddr_t)&ar) < 0) {
		(void) close(s);
		return (-1);
	}
	(void) close(s);
	if (ar.arp_flags & ATF_COM) {
		bcopy(&ar.arp_ha.sa_data, node, 6);
	} else
		return (-1);
	return (0);
}

/*
 * Fake up a valid Ethernet address based on gethostid().
 * This is likely to be unique to this machine, and that's
 * good enough for libuuid when we can't easily get our
 * real Ethernet address.
 */
static int
hostid_get(uuid_node_t *node)
{
	uint32_t hostid;

	hostid = (uint32_t)gethostid();
	if (hostid == 0 || hostid == ~0)
		return (-1);
	hostid = htonl(hostid);

	/*
	 * Like gen_ethernet_address(), use prefix:
	 * 8:0:... with the multicast bit set.
	 */
	node->nodeID[0] = 0x88;
	node->nodeID[1] = 0x00;
	memcpy(node->nodeID + 2, &hostid, 4);

	return (0);
}

/*
 * Name:	get_ethernet_address
 *
 * Description:	Obtains the system ethernet address, if possible.
 *
 * Returns:	0 on success, non-zero otherwise.  The system ethernet
 *		address is copied into the passed-in variable.
 *
 * Note:  This does NOT need to get the REAL Ethernet address.
 * This library only needs something that looks like an Ethernet
 * address and that's likely to be unique to this machine.  Also,
 * we really don't want to drag in libdlpi (etc) here so this just
 * tries an SIOCGARP ioctl, then a hostid-derived method.  If all
 * methods here fail, the caller generates an Ethernet address.
 */
int
get_ethernet_address(uuid_node_t *node)
{

	if (arp_get(node) == 0)
		return (0);

	if (hostid_get(node) == 0)
		return (0);

	return (-1);
}
