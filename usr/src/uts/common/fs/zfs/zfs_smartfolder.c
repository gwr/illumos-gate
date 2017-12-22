/*
 * Copyright 2009-2016 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#include "sys/zfs_smartfolder.h"
#include "sys/zfs_smartfolder_exp.h"
#include "sys/zfs_znode.h"
#include "sys/dmu_objset.h"
#include "sys/dsl_dataset.h"
#include "sys/dsl_dir.h"
#include "sys/dsl_prop.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include <sys/nvpair.h>

#ifdef	_KERNEL
#include <sys/zfs_vfsops.h>
#endif

int zfs_smartfolder = 1; /* enabled */
int zfs_smartfolder_nohidden = 1;

#define	ZFS_MAXPROPLEN	MAXPATHLEN

/*
 * ARGSUSED
 */
int
zfs_smartfolder_enabled(dsl_dataset_t *ds)
{
	uint64_t val;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	if (!zfs_smartfolder)
		return (B_FALSE);

	dsl_pool_config_enter(dp, FTAG);

	if (dsl_prop_get_int_ds(ds, "smartfolders", &val) != 0) {
		dsl_pool_config_exit(dp, FTAG);
		return (0);
	}

	DTRACE_PROBE1(xxx_smartfolder_en, uint64_t, val);

	dsl_pool_config_exit(dp, FTAG);
	return (val != 0);
}

static int
zfs_get_sharenfssmb(dsl_dataset_t *ds, char *sharenfs, char *sharesmb)
{
	int err;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	dsl_pool_config_enter(dp, FTAG);

	if ((err = dsl_prop_get_ds(ds, "sharenfs", 1, ZFS_MAXPROPLEN, sharenfs,
	    NULL)) != 0) {
		dsl_pool_config_exit(dp, FTAG);
		return (err);
	}

	if ((err = dsl_prop_get_ds(ds, "sharesmb", 1, ZFS_MAXPROPLEN, sharesmb,
	    NULL)) != 0) {
		dsl_pool_config_exit(dp, FTAG);
		return (err);
	}

	DTRACE_PROBE2(xxx_smartfolder_share, const char *, sharenfs,
	    const char *, sharesmb);

	dsl_pool_config_exit(dp, FTAG);
	return (0);
}

/*
 * buf must contain parent dir path
 */
static int
zfs_get_smartname(objset_t *os, const char *dirname, char *smartname)
{
	size_t len, dlen;

	ASSERT3P(os, !=, NULL);
	ASSERT3P(dirname, !=, NULL);
	ASSERT3P(smartname, !=, NULL);

	if (!zfs_smartfolder)
		return (ENOTSUP);

	if (smartname == NULL)
		return (EINVAL);

	dsl_dataset_name(os->os_dsl_dataset, smartname);

	len = strnlen(smartname, MAXPATHLEN);
	dlen = strnlen(dirname, MAXPATHLEN);

	if (len + dlen + 1 >= MAXPATHLEN)
		return (EINVAL);

	smartname[len] = '/';
	(void) strcpy(&smartname[len + 1], dirname);

	DTRACE_PROBE2(xxx_smartname,
	    char *, smartname,
	    char *, dirname);

	return (0);
}

/*
 * ARGSUSED
 */
static void
zfs_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
#ifdef	_KERNEL
	zfs_creat_t *zct = arg;
	zfs_create_fs(os, cr, zct->zct_zplprops, tx);
#endif
}

extern int zfs_fill_zplprops(const char *dataset, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci);

/*
 * ARGSUSED
 */
int
zfs_create_smartfolder(struct zfsvfs *zfsvfs, struct vnode *dvp, struct vnode *vp,
    const char *dirname, int flags, struct cred *cr, struct caller_context *ct)
{
	int err = EINVAL;
#ifdef	_KERNEL
	zfs_creat_t zct = { 0 };
	struct mounta ma;
	struct vfs *vfs;
	objset_t *os;
	boolean_t is_insensitive;
	char *ppath, *path;
	char *smartname;
	char *sharenfs, *sharesmb;
	refstr_t *mntpt;

	if (zfs_smartfolder_nohidden && dirname[0] == '.')
		return (err);

	ppath = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	smartname = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	sharenfs = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	sharesmb = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	if (zfsvfs->z_vfs->vfs_mntpt == NULL) {
		err = ENOENT;
		goto out;
	}
	/* Get parent dir path */
	if ((err = vnodetopath(NULL, dvp, ppath, MAXPATHLEN, cr)) != 0)
		goto out;

	/* Get dir path */
	if ((err = vnodetopath(NULL, vp, path, MAXPATHLEN, cr)) != 0)
		goto out;

	mntpt = vfs_getmntpoint(zfsvfs->z_vfs);
	if ((strcmp(refstr_value(mntpt), ppath) != 0)) {
		err = ENOTSUP;
		refstr_rele(mntpt);
		goto out;
	}
	DTRACE_PROBE3(xxx_smartpath, char *, ppath, char *, path,
	    char *, dirname);
	refstr_rele(mntpt);

	/* Need to check if dataset is mounted and parent dir == mountpoint */
	if ((err = zfs_get_smartname(zfsvfs->z_os, dirname, smartname)) != 0)
		goto out;

	if ((err = dmu_objset_hold(smartname, FTAG, &os)) == 0) {
		dmu_objset_rele(os, FTAG);
		err = EEXIST;
		goto out;
	}

	if ((err = zfs_get_sharenfssmb(dmu_objset_ds(zfsvfs->z_os), sharenfs,
	    sharesmb)) != 0) {
		goto out;
	}

	VERIFY(nvlist_alloc(&zct.zct_zplprops,
	    NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if ((err = zfs_fill_zplprops(smartname, NULL, zct.zct_zplprops,
	    &is_insensitive)) != 0) {
		nvlist_free(zct.zct_zplprops);
		goto out;
	}

	if ((err = dmu_objset_create_cred(smartname, DMU_OST_ZFS,
	    (flags & FIGNORECASE ?  DS_FLAG_CI_DATASET : 0),
	    zfs_create_cb, &zct, cr)) != 0) {
		nvlist_free(zct.zct_zplprops);
		goto out;
	}

	nvlist_free(zct.zct_zplprops);

	ma.spec = (char *)smartname;
	ma.dir = (char *)path;
	ma.flags = MS_SYSSPACE;
	ma.fstype = (char *)"zfs";
	ma.dataptr = NULL;
	ma.datalen = 0;
	ma.optptr = NULL;
	ma.optlen = 0;

	if ((err = domount("zfs", &ma, vp, kcred, &vfs)) == 0) {
		if (sharenfs[0] != '\0') {
			bool usetaskq = ct && ct->cc_flags & CC_HELDEXPLOCK;

			err = create_nfs_share(smartname, path, sharenfs,
			    kcred, usetaskq);
		}
		VFS_RELE(vfs);
	}
out:
	kmem_free(sharesmb, MAXPATHLEN);
	kmem_free(sharenfs, MAXPATHLEN);
	kmem_free(smartname, MAXPATHLEN);
	kmem_free(path, MAXPATHLEN);
	kmem_free(ppath, MAXPATHLEN);
#endif
	return (err);
}
