/*
 * Copyright 2009-2019 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#ifndef	_SYS_ZFS_SMARTFOLDER_H
#define	_SYS_ZFS_SMARTFOLDER_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_dataset;
struct zfsvfs;
struct vnode;
struct cred;
struct vsecattr;

int zfs_check_smartfolders_enabled(struct dsl_dataset *ds);
int zfs_check_smartroot(struct dsl_dataset *ds);
int zfs_check_smartfs(struct dsl_dataset *ds);
int zfs_create_smartfolder(struct zfsvfs *zfsvfs, struct vnode *dvp,
    struct vnode *vp, const char *dirname, int flags, struct cred *cr,
    struct vsecattr *vsecp);
int zfs_check_smartfolder(struct vnode *vp);
int zfs_smartfolder_mount(struct vnode *vp, const char *smartfs,
    const char *path);
int zfs_smartfolder_unmount(struct vnode *vp, char **dsnamep,
    char **pathp);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZFS_SMARTFOLDER_H */
