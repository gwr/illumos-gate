/*
 * Copyright 2009-2019 RackTop Systems LLC and/or its affiliates.
 * http://www.racktopsystems.com
 *
 * The methods and techniques utilized herein are considered TRADE SECRETS
 * and/or CONFIDENTIAL unless otherwise noted. REPRODUCTION or DISTRIBUTION
 * is FORBIDDEN, in whole and/or in part, except by express written permission
 * of RackTop Systems.
 */

#include "sys/zfs_smartfolder.h"
#include "sys/zfs_znode.h"
#include "sys/dmu_objset.h"
#include "sys/dsl_dataset.h"
#include "sys/dsl_dir.h"
#include "sys/dsl_prop.h"
#include "sys/zap.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include <sys/nvpair.h>
#include <sys/vnode.h>

#ifdef	_KERNEL
#include <sys/zfs_vfsops.h>
#endif

int zfs_smartfolder = 1; /* enabled */
int zfs_smartfolder_nohidden = 1;

#define	ZFS_MAXPROPLEN	MAXPATHLEN

int
zfs_check_smartfolders_enabled(dsl_dataset_t *ds)
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

	dsl_pool_config_exit(dp, FTAG);
	return (val != 0);
}

int
zfs_check_smartroot(dsl_dataset_t *ds)
{
	uint64_t val;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	dsl_pool_config_enter(dp, FTAG);

	if (dsl_prop_get_int_ds(ds, "smartfolders", &val) != 0) {
		dsl_pool_config_exit(dp, FTAG);
		return (0);
	}

	dsl_pool_config_exit(dp, FTAG);
	return (val != 0);
}

int
zfs_check_smartfs(dsl_dataset_t *ds)
{
	uint64_t val;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	dsl_pool_config_enter(dp, FTAG);

	if (dsl_prop_get_int_ds(ds, "smartfs", &val) != 0) {
		dsl_pool_config_exit(dp, FTAG);
		return (0);
	}

	dsl_pool_config_exit(dp, FTAG);
	return (val != 0);
}

/*
 * smartname must contain parent name
 */
static int
zfs_get_smartname(char *smartname, const char *dirname)
{
	size_t len, dlen;

	ASSERT3P(smartname, !=, NULL);
	ASSERT3P(dirname, !=, NULL);

	len = strnlen(smartname, ZFS_MAX_DATASET_NAME_LEN);
	dlen = strnlen(dirname, ZFS_MAX_DATASET_NAME_LEN);

	if (len + dlen + 1 >= ZFS_MAX_DATASET_NAME_LEN)
		return (EINVAL);

	smartname[len] = '/';
	(void) strcpy(&smartname[len + 1], dirname);

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
	zfs_create_fs(os, cr, zct->zct_zplprops, zct->zct_vsecattr, tx);
#endif
}

extern int zfs_fill_zplprops(const char *dataset, nvlist_t *createprops,
    nvlist_t *zplprops, boolean_t *is_ci);

/*
 * ARGSUSED
 */
int
zfs_check_create_smartfolder(struct zfsvfs *zfsvfs, const char *ppath)
{
	int err = 0;
#ifdef	_KERNEL
	refstr_t *mntpt;

	if (zfsvfs->z_vfs->vfs_mntpt == NULL)
		return (ENOENT);

	mntpt = vfs_getmntpoint(zfsvfs->z_vfs);
	if ((strcmp(refstr_value(mntpt), ppath) != 0)) {
		err = ENOTSUP;
		refstr_rele(mntpt);
		goto out;
	}

	refstr_rele(mntpt);
out:
#endif
	return (err);
}

/*
 * ARGSUSED
 */
int
zfs_create_smartfolder(struct zfsvfs *zfsvfs, struct vnode *dvp,
    struct vnode *vp, const char *dirname, int flags, struct cred *cr,
    vsecattr_t *vsecp)
{
	int err = EINVAL;
#ifdef	_KERNEL
	zfs_creat_t zct = { 0 };
	struct mounta ma;
	objset_t *os;
	boolean_t is_insensitive;
	char *path;
	char *smartname = NULL;

	if (zfs_smartfolder_nohidden && dirname[0] == '.')
		return (err);

	smartname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	/* Get parent dir path */
	if ((err = vnodetopath(NULL, dvp, path, MAXPATHLEN, cr)) != 0)
		goto out;

	/* Check parent dir is smartfolders parent dataset mountpoint */
	if ((err = zfs_check_create_smartfolder(zfsvfs, path)) != 0)
		goto out;

	/* Get dir path */
	if ((err = vnodetopath(NULL, vp, path, MAXPATHLEN, cr)) != 0)
		goto out;

	dsl_dataset_name(zfsvfs->z_os->os_dsl_dataset, smartname);

	if ((err = zfs_get_smartname(smartname, dirname)) != 0)
		goto out;

	/* Check if dataset does not exist */
	if ((err = dmu_objset_hold(smartname, FTAG, &os)) == 0) {
		dmu_objset_rele(os, FTAG);
		err = EEXIST;
		goto out;
	}

	VERIFY0(nvlist_alloc(&zct.zct_zplprops, NV_UNIQUE_NAME, KM_SLEEP));

	if ((err = zfs_fill_zplprops(smartname, NULL, zct.zct_zplprops,
	    &is_insensitive)) != 0) {
		nvlist_free(zct.zct_zplprops);
		goto out;
	}

	zct.zct_vsecattr = vsecp;

	/*
	 * Create dataset
	 */
	if ((err = dmu_objset_create_cred(smartname, DMU_OST_ZFS,
	    (flags & FIGNORECASE ?  DS_FLAG_CI_DATASET : 0),
	    zfs_create_cb, &zct, cr)) != 0) {
		nvlist_free(zct.zct_zplprops);
		goto out;
	}

	nvlist_free(zct.zct_zplprops);

	VERIFY0(nvlist_alloc(&zct.zct_props, NV_UNIQUE_NAME, KM_SLEEP));

	VERIFY0(nvlist_add_string(zct.zct_props, "sharesmb", "off"));
	VERIFY0(nvlist_add_string(zct.zct_props, "sharenfs", "off"));
	VERIFY0(nvlist_add_uint64(zct.zct_props, "smartfs", 1));

	VERIFY0(dsl_props_set(smartname, ZPROP_SRC_LOCAL, zct.zct_props));

	nvlist_free(zct.zct_props);

	/*
	 * Mount dataset on vp
	 */
	err = zfs_smartfolder_mount(vp, smartname, path);

out:
	kmem_free(path, MAXPATHLEN);
	kmem_free(smartname, ZFS_MAX_DATASET_NAME_LEN);
#endif
	return (err);
}

/*
 * ARGSUSED
 */
int
zfs_check_smartfolder(vnode_t *vp)
{
	int err = 0;
#ifdef	_KERNEL
	zfsvfs_t *zfsvfs;
	vfs_t *vfs;

	ASSERT3S(vp->v_type, ==, VDIR);

	if (!vn_ismntpt(vp))
		return (0);

	vfs = vn_mountedvfs(vp);
	zfsvfs = vfs->vfs_data;
	err = zfs_check_smartfs(dmu_objset_ds(zfsvfs->z_os));
#endif
	return (err);
}


/*
 * ARGSUSED
 */
int
zfs_smartfolder_mount(vnode_t *vp, const char *smartfs, const char *path)
{
	int err = 0;
#ifdef	_KERNEL
	struct mounta ma;
	struct vfs *vfs;

	ma.spec = (char *)smartfs;
	ma.dir = (char *)path;
	ma.flags = MS_SYSSPACE;
	ma.fstype = (char *)"zfs";
	ma.dataptr = NULL;
	ma.datalen = 0;
	ma.optptr = NULL;
	ma.optlen = 0;

	if ((err = domount("zfs", &ma, vp, kcred, &vfs)) == 0) {
		/*
		 * domount() adds ref and it should be released, same way as
		 * mount() does.
		 */
		VFS_RELE(vfs);
	}
#endif
	return (err);
}

/*
 * ARGSUSED
 */
int
zfs_smartfolder_unmount(vnode_t *vp, char **smartdsp, char **pathp)
{
	int err = 0;
#ifdef	_KERNEL
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs;
	vfs_t *vfs;
	objset_t *os;

	ASSERT3S(vp->v_type, ==, VDIR);

	*pathp = NULL;
	*smartdsp = NULL;

	if (vn_vfswlock_held(vp))
		return (SET_ERROR(EBUSY));

	if (!vn_ismntpt(vp)) {
		vn_vfsunlock(vp);
		return (SET_ERROR(EINVAL));
	}

	if (!zfs_check_smartfolder(vp)) {
		vn_vfsunlock(vp);
		return (SET_ERROR(EINVAL));
	}

	vfs = vn_mountedvfs(vp);
	zfsvfs = vfs->vfs_data;

	*smartdsp = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	*pathp = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	VERIFY0(vnodetopath(NULL, vp, *pathp, MAXPATHLEN, kcred));

	os = zfsvfs->z_os;

	dsl_dataset_name(dmu_objset_ds(os), *smartdsp);

	/* release read lock and take write one */
	vn_vfsunlock(vp);
	if (vn_vfswlock(vp)) {
		err = EBUSY;
		goto out;
	}

	if ((err = dounmount(vfs, 0, kcred)) != 0)
		goto out;

	return (0);
out:
	kmem_free(*pathp, MAXPATHLEN);
	kmem_free(*smartdsp, ZFS_MAX_DATASET_NAME_LEN);

	*pathp = NULL;
	*smartdsp = NULL;
#endif
	return (SET_ERROR(err));
}
