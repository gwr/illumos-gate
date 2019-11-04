/*
 * Copyright (C) 1996, 1997, 1998, 1999 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

/*
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
