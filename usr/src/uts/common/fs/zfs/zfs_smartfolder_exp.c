/*
 * Copyright 2009-2016 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#include <sys/kmem.h>
#include <sys/cpuvar.h>
#include <nfs/export.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <sharefs/share.h>
#include <sys/door.h>
#include <sys/zfs_smartfolder_exp.h>

static void door_share_call(smartfolder_exp_data_t *);

#ifdef	_KERNEL
extern kmutex_t sfdh_lock;
extern door_handle_t smartfolder_dh;
#endif

/*
 *
 */
int
zfs_smartfolder_init(void)
{
	return (0);
}

/*
 *
 */
void
zfs_smartfolder_fini(void)
{
}

/* ARGSUSED */
int
create_nfs_share(char *dsname, char *path, char *sharenfs, struct cred *cr)
{
#ifdef	_KERNEL
	smartfolder_exp_data_t *sed;

	ASSERT3P(dsname, !=, NULL);
	ASSERT3P(path, !=, NULL);
	ASSERT3P(sharenfs, !=, NULL);

	if ((sed = kmem_alloc(sizeof (*sed), KM_SLEEP)) == NULL)
		return (ENOMEM);

	(void) strncpy(sed->sed_dsname, dsname, sizeof (sed->sed_dsname));
	(void) strncpy(sed->sed_path, path, sizeof (sed->sed_path));
	(void) strncpy(sed->sed_sharenfs, sharenfs, sizeof (sed->sed_sharenfs));
	sed->sed_sharesmb[0] = '\0';

	door_share_call(sed);
#endif

	return (0);
}

static void
door_share_call(smartfolder_exp_data_t *sed)
{
#ifdef	_KERNEL
	int err;
	smartfolder_exp_res_t ser;
	door_arg_t door_args;
	door_handle_t sfdh;

	mutex_enter(&sfdh_lock);
	if (smartfolder_dh == NULL) {
		cmn_err(CE_WARN, "smart door handle is NULL");
		mutex_exit(&sfdh_lock);
		goto out;
	}

	sfdh = smartfolder_dh;
	door_ki_hold(sfdh);
	mutex_exit(&sfdh_lock);

	door_args.data_ptr = (char *)sed;
	door_args.data_size = sizeof (*sed);
	door_args.desc_ptr = NULL;
	door_args.desc_num = 0;
	door_args.rbuf = (char *)&ser;
	door_args.rsize = sizeof (struct smartfolder_exp_res);

	if ((err = door_ki_upcall(sfdh, &door_args)) != 0) {
		cmn_err(CE_WARN, "Failed to make smartfold door_ki_upcall: %d",
		    err);
	}

	door_ki_rele(sfdh);

out:
	kmem_free(sed, sizeof (*sed));
#endif
}
