/*
 * Copyright 2009-2015 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#ifndef	_SYS_ZFS_SMARTFOLDER_H
#define	_SYS_ZFS_SMARTFOLDER_H

#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct vnode;
struct cred;

boolean_t zfs_smartfolder_enabled(objset_t *os);
int zfs_get_smartname(objset_t *os, const const char *dirname, char *path);
int zfs_create_smartfolder(struct vnode *vn, struct cred *cr,
    const char *smartpath, int flags);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZFS_SMARTFOLDER_H */
