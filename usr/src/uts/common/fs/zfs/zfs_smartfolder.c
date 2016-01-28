/*
 * Copyright 2009-2015 RackTop Systems LLC and/or its affiliates.
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
#include "sys/vfs.h"
#include "sys/mount.h"
#include <sys/nvpair.h>

int zfs_smartfolder = 1; /* enabled */
int zfs_smartfolder_kcred;

/*
 * ARGSUSED
 */
boolean_t
zfs_smartfolder_enabled(objset_t *os)
{
	uint64_t val;
	dsl_pool_t *dp = os->os_dsl_dataset->ds_dir->dd_pool;

	if (!zfs_smartfolder)
		return (B_FALSE);

	dsl_pool_config_enter(dp, FTAG);

	if (dsl_prop_get_int_ds(dmu_objset_ds(os), "smartfolders", &val) != 0) {
		dsl_pool_config_exit(dp, FTAG);
		return (B_FALSE);
	}

	dsl_pool_config_exit(dp, FTAG);
	return (val != 0);
}

/*
 * buf must contain parent dir path
 */
int
zfs_get_smartname(objset_t *os, const char *dirname, char *path,
    char *smartname)
{
	size_t len, slen, dlen;
	char *p;

	ASSERT3P(os, !=, NULL);
	ASSERT3P(dirname, !=, NULL);
	ASSERT3P(path, !=, NULL);
	ASSERT3P(smartname, !=, NULL);
	ASSERT3S(path[0], ==, '/');

	if (!zfs_smartfolder)
		return (ENOTSUP);

	if (smartname == NULL)
		return (EINVAL);

	dsl_dataset_name(os->os_dsl_dataset, smartname);

	DTRACE_PROBE2(xxx_smartfolder,
	    char *, smartname,
	    char *, path);

	len = strnlen(path, MAXPATHLEN);
	slen = strnlen(smartname, MAXPATHLEN);
	dlen = strnlen(dirname, MAXPATHLEN);

	if ((len + dlen + 1 >= MAXPATHLEN) ||
	    (slen + dlen + 1 >= MAXPATHLEN))
		return (EINVAL);

	p = path + len;
	*p++ = '/';
	(void) strcpy(p, dirname);
	p = smartname + slen;
	*p++ = '/';
	(void) strcpy(p, dirname);

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
zfs_create_smartfolder(struct vnode *vn, struct cred *cr,
    const char *smartpath, const char *smartname, int flags)
{
	int err;
#ifdef	_KERNEL
	zfs_creat_t zct = { 0 };
	struct mounta ma;
	struct vfs *vfs;
	objset_t *os;
	boolean_t is_insensitive;

	if ((err = dmu_objset_hold(smartname, FTAG, &os)) == 0) {
		dmu_objset_rele(os, FTAG);
		return (EEXIST);
	}

	VERIFY(nvlist_alloc(&zct.zct_zplprops,
	    NV_UNIQUE_NAME, KM_SLEEP) == 0);

	if ((err = zfs_fill_zplprops(smartname, NULL, zct.zct_zplprops,
	    &is_insensitive)) != 0) {
		nvlist_free(zct.zct_zplprops);
		return (err);
	}

	if ((err = dmu_objset_create(smartname, DMU_OST_ZFS,
	    (flags & FIGNORECASE ?  DS_FLAG_CI_DATASET : 0),
	    zfs_create_cb, &zct)) != 0) {
		nvlist_free(zct.zct_zplprops);
		return (err);
	}

	nvlist_free(zct.zct_zplprops);

	ma.spec = (char *)smartname;
	ma.dir = (char *)smartpath;
/*
 *	ma.flags = MS_SYSSPACE | MS_NOMNTTAB;
 */
	ma.flags = MS_SYSSPACE;
	ma.fstype = (char *)"zfs";
	ma.dataptr = NULL;
	ma.datalen = 0;
	ma.optptr = NULL;
	ma.optlen = 0;

	err = domount("zfs", &ma, vn, (zfs_smartfolder_kcred ? kcred : cr),
	    &vfs);
	VFS_RELE(vfs);
#endif
	return (err);
}
