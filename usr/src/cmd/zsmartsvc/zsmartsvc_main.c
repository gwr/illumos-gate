/*
 * Copyright 2009-2016 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#include <locale.h>
#include <libintl.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <libzfs_core.h>
#include <libzfs.h>
#include <door.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/zfs_smartfolder_exp.h>

door_server_procedure_t smart_share;
libzfs_handle_t *lzh;

/*
 *
 */
int
smart_share_nfs(struct smartfolder_exp_data *sed)
{
	int err;

	zfs_handle_t *zh = zfs_open(lzh, sed->sed_dsname, ZFS_TYPE_FILESYSTEM);
	err = zfs_share_nfs(zh);
	zfs_close(zh);
	return (err);
}

/* ARGSUSED */
void
smart_share(void *cookie, char *argp, size_t arg_size,
    door_desc_t *dp, uint_t n_desc)
{
	smartfolder_exp_res_t	ser;
	smartfolder_exp_data_t	*sed;
	sed = (smartfolder_exp_data_t *)argp;

	ser.ser_nfs_err = smart_share_nfs(sed);
	ser.ser_smb_err = 0;

	(void) door_return((char *)&ser, sizeof (ser), NULL, 0);
}

static int
smart_kcall(int doorfd)
{
	return (lzc_smartdoor(doorfd));
}

#define	SMARTFS_DOOR	"/var/run/smartfs_door"

static int
smart_svc(void)
{
	int err;
	int doorfd = -1;
#ifdef DEBUG
	int dfd;
#endif

	if ((doorfd = door_create(smart_share, NULL,
	    DOOR_REFUSE_DESC | DOOR_NO_CANCEL)) == -1) {
		fprintf(stderr, "Unable to create door: %m\n");
		return (1);
	}

#ifdef DEBUG
	/*
	 * Create a file system path for the door
	 */
	if ((dfd = open(SMARTFS_DOOR, O_RDWR|O_CREAT|O_TRUNC,
				S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)) == -1) {
		fprintf(stderr, "Unable to open %s: %m\n", SMARTFS_DOOR);
		(void) close(doorfd);
		return (1);
	}

	/*
	 * Clean up any stale associations
	 */
	(void) fdetach(SMARTFS_DOOR);

	/*
	 * Register in namespace to pass to the kernel to door_ki_open
	 */
	if (fattach(doorfd, SMARTFS_DOOR) == -1) {
		fprintf(stderr, "Unable to fattach door: %m\n");
		(void) close(dfd);
		(void) close(doorfd);
		return (1);
	}
	(void) close(dfd);
#endif

	/*
	 * Now that we're actually running, go
	 * ahead and flush the kernel flushes
	 * Pass door name to kernel for door_ki_open
	 */
	if ((err = smart_kcall(doorfd)) != 0) {
		fprintf(stderr, "Failed to set up smartdoor\n");
		return (err);
	}

	/*
	 * Wait for incoming calls
	 */
	/*CONSTCOND*/
	while (1)
		(void) pause();

	fprintf(stderr, gettext("Door server exited"));
	return (10);
}

int
main(int argc, char **argv)
{
	int err = 0;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if ((lzh = libzfs_init()) == NULL) {
		(void) fprintf(stderr, gettext("internal error: failed to "
		    "initialize ZFS library\n"));
		return (1);
	}

	if ((err = smart_svc()) != 0)
		fprintf(stderr, gettext("smart_svc error: %d\n"), err);

	libzfs_fini(lzh);

	return (err);
}
