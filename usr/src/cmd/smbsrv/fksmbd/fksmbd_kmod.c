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
 * These replace functions (mostly of the same name) in
 * $SRC/lib/smbsrv/libsmb/common/smb_kmod.c
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioccom.h>
#include <sys/param.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_ioctl.h>
#include "smbd.h"


int
smb_kmod_bind(void)
{
	return (fksmbsrv_drv_open());
}

void
smb_kmod_unbind(void)
{
	(void) fksmbsrv_drv_close();
}

void
fksmb_override_config(smb_ioc_header_t *iochdr, uint32_t len)
{
	smb_ioc_cfg_t *ioc = (smb_ioc_cfg_t *)iochdr;
	if (len != sizeof (*ioc))
		abort();

	/* XXX: Fix dynamic taskq instead */
	ioc->maxconnections = 10;
	ioc->maxworkers = 20;
}


/*
 * Note: This replaces the function of the name name in libsmb,
 * which would use ioctl(2) to enter the smbsrv driver.
 * Here, we call the smbsrv code directly instead.
 */
int
smb_kmod_ioctl(int cmd, smb_ioc_header_t *ioc, uint32_t len)
{

	_NOTE(ARGUNUSED(len));

	if (cmd == SMB_IOC_CONFIG) {
		/* Some overrides... */
		fksmb_override_config(ioc, len);
	}

	return (fksmbsrv_drv_ioctl(cmd, ioc));
}

/* ARGSUSED */
int
smb_kmod_start(int opipe, int lmshr, int udoor)
{
	smb_ioc_start_t ioc;

	/* These three are unused */
	ioc.opipe = -1;
	ioc.lmshrd = -1;
	ioc.udoor = -1;

	/* These are the "door" dispatch callbacks */
	ioc.lmshr_func = NULL; /* not used */
	ioc.opipe_func = (void *)fksmbd_opipe_dispatch;
	ioc.udoor_func = (void *)fksmbd_door_dispatch;

	return (smb_kmod_ioctl(SMB_IOC_START, &ioc.hdr, sizeof (ioc)));
}
