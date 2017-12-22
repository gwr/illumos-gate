/*
 * Copyright 2009-2016 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#ifndef	_SYS_ZFS_SMARTFOLDER_EXP_H
#define	_SYS_ZFS_SMARTFOLDER_EXP_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/param.h>

typedef struct smartfolder_exp_data {
	char	sed_dsname[MAXPATHLEN];
	char	sed_path[MAXPATHLEN];
	char	sed_sharenfs[MAXPATHLEN];
	char	sed_sharesmb[MAXPATHLEN];
} smartfolder_exp_data_t;

typedef struct smartfolder_exp_res {
	int	ser_nfs_err;
	int	ser_smb_err;
} smartfolder_exp_res_t;

#ifdef	_KERNEL
#include <sys/stdbool.h>

int create_nfs_share(char *smartname, char *path, char *sharenfs, struct cred *cr,
    bool use_taskq);
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZFS_SMARTFOLDER_EXP_H */
