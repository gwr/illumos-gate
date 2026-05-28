
// This file and its contents are supplied under the terms of the
// Common Development and Distribution License ("CDDL"), version 1.0.
// You may only use this file in accordance with the terms of version
// 1.0 of the CDDL.
//
// A full copy of the text of the CDDL should have accompanied this
// source.  A copy of the CDDL is also available via the Internet at
// http://www.illumos.org/license/CDDL.



// intrd-move.cc - production interrupt move operations
//
// Implements intr_move() and is_apic() for the production intrd binary.
// These wrap the PCITOOL ioctls on /devices<buspath>:intr.
//
// Functionally equivalent to Sun::Solaris::Intrs (Intrs.xs).


#include <sys/types.h>
#include <sys/pci_tools.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>

#include "intrd_sys.h"


// Open the PCITOOL device node for the bus at path.
// Returns an open file descriptor, or -1 with errno set on failure.

static int
open_dev(const char *path)
{
	char intrpath[MAXPATHLEN];
	int n;

	n = snprintf(intrpath, sizeof (intrpath), "/devices%s:intr", path);
	if (n < 0 || n >= (int)sizeof (intrpath)) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	return (open(intrpath, O_RDWR));
}


// Move interrupt ino on buspath from oldcpu to newcpu.
// num_ino > 1 indicates an MSI group move.
// Returns 0 on success, or -1 with errno set on failure.

int
intr_move(const char *buspath, int oldcpu, int ino, int newcpu, int num_ino)
{
	pcitool_intr_set_t iset;
	int fd, ret;

	if ((fd = open_dev(buspath)) < 0)
		return (-1);

	iset.old_cpu = oldcpu;
	iset.ino = ino;
	iset.cpu_id = newcpu;
	iset.flags = (num_ino > 1) ? PCITOOL_INTR_FLAG_SET_GROUP : 0;
	iset.user_version = PCITOOL_VERSION;

	ret = ioctl(fd, PCITOOL_DEVICE_SET_INTR, &iset);
	(void) close(fd);
	return (ret);
}


// Returns 0 if the bus at buspath does not use a pcplusmp or APIX APIC,
// 1 if it does, or -1 with errno set on failure.

int
is_apic(const char *buspath)
{
	pcitool_intr_info_t iinfo;
	int fd, ret;

	if ((fd = open_dev(buspath)) < 0)
		return (-1);

	iinfo.user_version = PCITOOL_VERSION;
	ret = ioctl(fd, PCITOOL_SYSTEM_INTR_INFO, &iinfo);
	(void) close(fd);

	if (ret == -1)
		return (-1);

	return (iinfo.ctlr_type == PCITOOL_CTLR_TYPE_PCPLUSMP ||
	    iinfo.ctlr_type == PCITOOL_CTLR_TYPE_APIX);
}
