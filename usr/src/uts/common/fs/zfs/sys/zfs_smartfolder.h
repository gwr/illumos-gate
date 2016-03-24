/*
 * Copyright 2009-2016 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#ifndef	_SYS_ZFS_SMARTFOLDER_H
#define	_SYS_ZFS_SMARTFOLDER_H

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_dataset;
struct zfsvfs;
struct vnode;
struct cred;

int zfs_smartfolder_init(void);
void zfs_smartfolder_fini(void);
int zfs_smartfolder_enabled(struct dsl_dataset *ds);
int zfs_create_smartfolder(struct zfsvfs *zfsvfs, struct vnode *dvp,
    struct vnode *vp, const char *dirname, int flags, struct cred *cr);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZFS_SMARTFOLDER_H */
