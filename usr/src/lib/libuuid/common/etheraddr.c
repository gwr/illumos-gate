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
 * Copyright 2019 Nexenta by DDN, Inc. All rights reserved.
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
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#include "etheraddr.h"

/*
 * Name:	get_ethernet_address
 *
 * Description:	Obtains a system ethernet address (any will do)
 *		used as a unique identifier by uuid_generate_time().
 *
 * Returns:	0 on success, non-zero otherwise.  The system ethernet
 *		address is copied into the passed-in variable.
 *
 * If this fails, the caller uses a randomly generated node ID instead,
 * so a failure here will not cause the uuid_create call to fail.
 * In fact, most methods here WILL fail when this library is called
 * from a process with ordinary privileges, and that's OK.
 */
int
get_ethernet_address(uuid_node_t *node)
{
	char buf[1024];
	int		sd;
	struct lifreq	ifr, *ifrp;
	struct lifconf	ifc;
	int		n, i;
	unsigned char	*a;
	struct sockaddr_dl *sdlp;

	sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0)
		return (-1);

	memset(buf, 0, sizeof(buf));
	ifc.lifc_len = sizeof(buf);
	ifc.lifc_buf = buf;
	if (ioctl (sd, SIOCGLIFCONF, (char *)&ifc) < 0) {
		close(sd);
		return (-1);
	}
	n = ifc.lifc_len;
	for (i = 0; i < n; i+= sizeof (*ifrp) ) {
		ifrp = (struct lifreq *)((char *)ifc.lifc_buf + i);

		memset(&ifr, 0, sizeof (ifr));
		strncpy(ifr.lifr_name, ifrp->lifr_name, LIFNAMSIZ);

		if (ioctl(sd, SIOCGLIFHWADDR, &ifr) < 0)
			continue;

		sdlp = (struct sockaddr_dl *) &ifr.lifr_addr;
		if ((sdlp->sdl_family != AF_LINK) || (sdlp->sdl_alen != 6))
			continue;
		a = (unsigned char *) &sdlp->sdl_data[sdlp->sdl_nlen];

		if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
			continue;

		memcpy(node, a, 6);
		close(sd);
		return (0);
	}
	close(sd);

	return (-1);
}
