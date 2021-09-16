/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2022 Tintri by DDN, Inc. All rights reserved.
 */

/*
 * Implements Reparse Point support using Extended Attributes.
 */

#include <fs/fs_reparse.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/nbmlock.h>
#include <sys/sdt.h>

const uint32_t reparse_packed_cur_version = 1;
uint_t reparse_vsd_key = 0;
kmem_cache_t *reparse_vsd_cache;

static void reparse_free_data_locked(reparse_data_t *);
static reparse_data_t *reparse_alloc_data(reparse_vsd_t *, int);

/*
 * Reparse Points are stored as packed NVLs. The NVL contains the tag
 * and the data.
 *
 * Reads treat 0-length reparse data as 'no reparse point'.
 * Writes first truncate the file, write the data, then set the REPARSE bit.
 * Deletes remove the file, then unset the REPARSE bit.
 *
 * Together, this allows operations to 'interrupt' each other but allow
 * the reparse data to remain valid; Reads don't check the REPARSE bit
 * (and so can't be interrupted); Writes truncate the file before writing;
 * and Deletes unset the bit before removing the file.
 * With this, the worse case scenarios is: the bit is set but there is no data.
 * In this case, we may waste time looking up a file that has no data.
 * That's not catastrophic, and can be resolved by the user
 * (by deleting and recreating the now-empty file).
 *
 * We use kcred to avoid problems with differences in security
 * information between the XATTR objects and the underlying file.
 *
 * The callers are expected to perform appropriate access checks.
 * If this interface is extended to non-SMB callers, we may want to do
 * access checks (for WRITE_DATA/WRITE_ATTR) here, with an argument
 * to skip these for handle-based callers like SMB.
 */

static reparse_vsd_t *
reparse_get_vsd(vnode_t *vp)
{
	reparse_vsd_t *rpv;

	mutex_enter(&vp->v_vsd_lock);
	rpv = (reparse_vsd_t *)vsd_get(vp, reparse_vsd_key);
	if (rpv == NULL) {
		rpv = kmem_cache_alloc(reparse_vsd_cache, KM_SLEEP);
		VERIFY(vsd_set(vp, reparse_vsd_key, (void *)rpv) == 0);
	}
	mutex_exit(&vp->v_vsd_lock);

	return (rpv);
}

int
reparse_set_data(vnode_t *vp, uint32_t tag, uint8_t *data, uint_t datalen,
    cred_t *cr, caller_context_t *cctx)
{
	xvattr_t xva = {
		.xva_vattr.va_mask = AT_MODE|AT_SIZE|AT_TYPE|AT_UID|AT_GID,
		.xva_vattr.va_type = VREG,
		.xva_vattr.va_mode = 0640,
		.xva_vattr.va_size = 0, /* truncate */
		.xva_vattr.va_uid = crgetuid(cr),
		.xva_vattr.va_gid = crgetgid(cr)
	};
	iovec_t iov;
	uio_t uio = {
		.uio_iov = &iov,
		.uio_iovcnt = 1,
		.uio_segflg = UIO_SYSSPACE,
		.uio_extflg = UIO_COPY_DEFAULT,
		.uio_llimit = MAXOFFSET_T
	};
	reparse_vsd_t *rpv;
	reparse_data_t *rp = NULL;
	vnode_t *xattrdirvp = NULL, *rp_vp = NULL;
	cred_t *kcr;
	nvlist_t *nvl = NULL;
	xoptattr_t *xoap;
	char *buf = NULL;
	size_t buflen = 0;
	int rc;
	boolean_t crit = B_FALSE, lock_held = B_FALSE;

	if (tag == REPARSE_TAG_LEGACY ||
	    vp->v_type == VLNK ||
	    !vfs_has_feature(vp->v_vfsp, VFSFT_XVATTR))
		return (SET_ERROR(ENOTSUP));

	kcr = zone_kcred();

	if ((rc = VOP_LOOKUP(vp, "", &xattrdirvp, NULL,
	    LOOKUP_XATTR | CREATE_XATTR_DIR, NULL, kcr, cctx, NULL,
	    NULL)) != 0) {
		cmn_err(CE_NOTE, "!%s: XATTR lookup failed: %d", __func__, rc);
		return (rc);
	}

	if ((rc = nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP)) != 0) {
		cmn_err(CE_NOTE, "!%s: nvl_alloc failed: %d", __func__, rc);
		goto errout;
	}

	if ((rc = nvlist_add_uint32(nvl, "version",
	    reparse_packed_cur_version)) != 0)
		goto errout;
	if ((rc = nvlist_add_uint32(nvl, "reparse_tag",
	    tag)) != 0)
		goto errout;
	if ((rc = nvlist_add_uint8_array(nvl, "reparse_data",
	    data, datalen)) != 0)
		goto errout;

	if ((rc = nvlist_pack(nvl, &buf, &buflen,
	    NV_ENCODE_XDR, KM_SLEEP)) != 0) {
		cmn_err(CE_NOTE, "!%s: nvl pack failed: %d", __func__, rc);
		goto errout;
	}

	iov.iov_base = buf;
	iov.iov_len = buflen;
	uio.uio_resid = buflen;

	rpv = reparse_get_vsd(vp);
	rp = reparse_alloc_data(rpv, KM_SLEEP);
	lock_held = B_TRUE;
	mutex_enter(&rpv->rpv_lock);

	/* use xva_vattr as our vattr_t */
	if ((rc = VOP_CREATE(xattrdirvp, "SUNWreparse", &xva.xva_vattr,
	    NONEXCL, 0, &rp_vp, kcr, 0, cctx, NULL)) != 0) {
		cmn_err(CE_WARN, "!%s: failed to create reparse xattr: %d",
		    __func__, rc);
		goto errout;
	}

	if (nbl_need_check(rp_vp)) {
		int svmand;

		crit = B_TRUE;
		nbl_start_crit(rp_vp, RW_READER);
		if ((rc = nbl_svmand(rp_vp, kcr, &svmand)) != 0) {
			goto errout;
		}

		if (nbl_conflict(rp_vp, NBL_WRITE, uio.uio_loffset, buflen,
		    svmand, cctx)) {
			rc = SET_ERROR(ERANGE);
			goto errout;
		}
	}

	(void) VOP_RWLOCK(rp_vp, V_WRITELOCK_TRUE, cctx);
	rc = VOP_WRITE(rp_vp, &uio, 0, kcr, cctx);
	VOP_RWUNLOCK(rp_vp, V_WRITELOCK_TRUE, cctx);

	if (crit) {
		nbl_end_crit(rp_vp);
		crit = B_FALSE;
	}

	if (rc == 0 && uio.uio_resid != 0)
		rc = SET_ERROR(EIO);

	if (rc != 0) {
		cmn_err(CE_WARN, "!%s: failed writing reparse data: %d",
		    __func__, rc);
		goto errout;
	}

	kmem_free(buf, buflen);
	buf = NULL;

	/*
	 * The write should have set the size of the xattr to at least
	 * the size of the packed NVL.
	 */

	xva_init(&xva);
	xoap = xva_getxoptattr(&xva);
	ASSERT(xoap != NULL);

	XVA_SET_REQ(&xva, XAT_REPARSE);
	xoap->xoa_reparse = 1;
	XVA_SET_REQ(&xva, XAT_REPARSE_TAG);
	xoap->xoa_reparse_tag = tag;

	if (vp->v_type == VREG) {
		XVA_SET_REQ(&xva, XAT_ARCHIVE);
		xoap->xoa_archive = 1;
	}

	/* Set XAT_REPARSE on the main object. */
	if ((rc = VOP_SETATTR(vp, &xva.xva_vattr, 0, kcr, cctx)) != 0) {
		cmn_err(CE_WARN, "!%s: failed to set Reparse attribute: %d",
		    __func__, rc);
		(void) VOP_REMOVE(xattrdirvp, "SUNWreparse", kcr, cctx, 0);
		goto errout;
	}

	if (rpv->rpv_data != NULL)
		reparse_free_data_locked(rpv->rpv_data);

	if ((rc = nvlist_lookup_uint8_array(nvl, "reparse_data", &data,
	    &datalen)) != 0 || datalen == 0) {
		cmn_err(CE_NOTE, "!%s: lookup reparse_data failed: %d",
		    __func__, rc);
		reparse_free_data_locked(rp);
		nvlist_free(nvl);
		rp = NULL;
		rpv->rpv_data = NULL;
	} else {
		rp->rp_tag = tag;
		rp->rp_data = data;
		rp->rp_len = datalen;
		rp->rp_nvl = nvl;

		rpv->rpv_data = rp;
	}
	mutex_exit(&rpv->rpv_lock);

	VN_RELE(rp_vp);
	VN_RELE(xattrdirvp);

	return (0);

errout:
	if (rp != NULL) {
		ASSERT(lock_held);
		reparse_free_data_locked(rp);
	}
	if (lock_held)
		mutex_exit(&rpv->rpv_lock);
	if (crit)
		nbl_end_crit(rp_vp);
	nvlist_free(nvl);
	if (buf != NULL)
		kmem_free(buf, buflen);
	if (rp_vp != NULL)
		VN_RELE(rp_vp);
	if (xattrdirvp != NULL)
		VN_RELE(xattrdirvp);

	return (rc);
}

int
reparse_get_data(vnode_t *vp, reparse_data_t **rpp, caller_context_t *cctx)
{
	vattr_t va = { .va_mask = AT_SIZE };
	iovec_t iov;
	uio_t uio = {
		.uio_iov = &iov,
		.uio_iovcnt = 1,
		.uio_segflg = UIO_SYSSPACE,
		.uio_extflg = UIO_COPY_CACHED
	};
	reparse_data_t *rp;
	reparse_vsd_t *rpv;
	vnode_t *xattrdirvp = NULL, *rp_vp = NULL;
	cred_t *kcr;
	char *buf = NULL;
	nvlist_t *nvl = NULL;
	uint8_t *data;
	size_t buflen = 0;
	uint32_t u32;
	uint_t datalen = 0;
	int rc;
	boolean_t crit = B_FALSE;

	ASSERT(rpp != NULL);

	rpv = reparse_get_vsd(vp);
	mutex_enter(&rpv->rpv_lock);
	rp = rpv->rpv_data;
	if (rp != NULL) {
		rp->rp_cnt++;
		mutex_exit(&rpv->rpv_lock);
		*rpp = rp;
		return (0);
	}

	if (vp->v_type == VLNK) {
		nvl = reparse_init();

		if (nvl == NULL)
			return (SET_ERROR(ENOMEM));

		rc = reparse_vnode_parse(vp, nvl);
		if (rc != 0) {
			reparse_free(nvl);
			return (rc);
		}

		rp = reparse_alloc_data(rpv, KM_SLEEP);
		rp->rp_tag = REPARSE_TAG_LEGACY;
		rp->rp_len = 0;
		rp->rp_data = NULL;
		rp->rp_nvl = nvl;

		rp->rp_cnt++;
		rpv->rpv_data = rp;
		mutex_exit(&rpv->rpv_lock);
		*rpp = rp;
		return (0);
	}

	kcr = zone_kcred();
	if ((rc = VOP_LOOKUP(vp, "", &xattrdirvp, NULL,
	    LOOKUP_XATTR, NULL, kcr, cctx, NULL, NULL)) != 0) {
		if (rc != ENOENT)
			cmn_err(CE_NOTE, "!%s: xattrdir lookup failed: %d",
			    __func__, rc);
		return (rc);
	}

	if ((rc = VOP_LOOKUP(xattrdirvp, "SUNWreparse", &rp_vp, NULL, 0, NULL,
	    kcr, cctx, NULL, NULL)) != 0) {
		if (rc != ENOENT)
			cmn_err(CE_NOTE, "!%s: reparse lookup failed: %d",
			    __func__, rc);
		goto errout;
	}

	if ((rc = VOP_GETATTR(rp_vp, &va, 0, kcr, NULL)) != 0) {
		cmn_err(CE_NOTE, "!%s: getattr failed: %d", __func__, rc);
		goto errout;
	}

	if (va.va_size == 0) {
		rc = SET_ERROR(ENOENT);
		goto errout;
	}

	if (va.va_size < REPARSE_XATTR_MIN_SIZE ||
	    va.va_size > REPARSE_XATTR_MAX_SIZE) {
		rc = SET_ERROR(EINVAL);
		goto errout;
	}

	buflen = va.va_size;
	buf = kmem_alloc(buflen, KM_SLEEP);

	iov.iov_base = buf;
	iov.iov_len = buflen;
	uio.uio_resid = buflen;

	if (nbl_need_check(rp_vp)) {
		int svmand;

		crit = B_TRUE;
		nbl_start_crit(rp_vp, RW_READER);
		if ((rc = nbl_svmand(rp_vp, kcr, &svmand)) != 0) {
			goto errout;
		}

		if (nbl_conflict(rp_vp, NBL_READ, uio.uio_loffset, buflen,
		    svmand, cctx)) {
			rc = SET_ERROR(ERANGE);
			goto errout;
		}
	}

	(void) VOP_RWLOCK(rp_vp, V_WRITELOCK_FALSE, cctx);
	rc = VOP_READ(rp_vp, &uio, 0, kcr, cctx);
	VOP_RWUNLOCK(rp_vp, V_WRITELOCK_FALSE, cctx);

	if (crit) {
		nbl_end_crit(rp_vp);
		crit = B_FALSE;
	}

	if (rc != 0) {
		cmn_err(CE_NOTE, "!%s: read failed: %d", __func__, rc);
		goto errout;
	}

	/* Treat 0 length reparse as no reparse data */
	if (uio.uio_resid == buflen) {
		rc = SET_ERROR(ENOENT);
		goto errout;
	}

	if ((rc = nvlist_unpack(buf, buflen, &nvl, KM_SLEEP)) != 0) {
		cmn_err(CE_NOTE, "!%s: nvlist_unpack failed: %d", __func__, rc);
		goto errout;
	}

	kmem_free(buf, buflen);
	buf = NULL;

	if ((rc = nvlist_lookup_uint32(nvl, "version", &u32)) != 0) {
		cmn_err(CE_NOTE, "!%s: lookup version failed: %d", __func__, rc);
		goto errout;
	}

	if (u32 != reparse_packed_cur_version) {
		rc = SET_ERROR(EINVAL);
		cmn_err(CE_NOTE, "!%s: bad reparse version: %d", __func__, u32);
		goto errout;
	}

	u32 = 0;
	if ((rc = nvlist_lookup_uint32(nvl, "reparse_tag", &u32)) != 0) {
		cmn_err(CE_NOTE, "!%s: lookup reparse_tag failed: %d",
		    __func__, rc);
		goto errout;
	}

	if ((rc = nvlist_lookup_uint8_array(nvl, "reparse_data", &data,
	    &datalen)) != 0) {
		cmn_err(CE_NOTE, "!%s: lookup reparse_data failed: %d",
		    __func__, rc);
		goto errout;
	}

	if (datalen == 0) {
		rc = SET_ERROR(EINVAL);
		cmn_err(CE_NOTE, "!%s: no data", __func__);
		goto errout;
	}

	rp = reparse_alloc_data(rpv, KM_SLEEP);
	rp->rp_tag = u32;
	rp->rp_data = data;
	rp->rp_len = datalen;
	rp->rp_nvl = nvl;

	/*
	 * Take ref for the caller.
	 * rp_cnt should be two - one for caller, one for rpv_data.
	 */
	rp->rp_cnt++;
	rpv->rpv_data = rp;
	mutex_exit(&rpv->rpv_lock);
	*rpp = rp;

	VN_RELE(rp_vp);
	VN_RELE(xattrdirvp);

	return (0);

errout:
	mutex_exit(&rpv->rpv_lock);
	if (crit)
		nbl_end_crit(rp_vp);
	nvlist_free(nvl);
	if (buf != NULL)
		kmem_free(buf, buflen);
	if (rp_vp != NULL)
		VN_RELE(rp_vp);
	if (xattrdirvp != NULL)
		VN_RELE(xattrdirvp);
	return (rc);
}

int
reparse_remove_data(vnode_t *vp, caller_context_t *cctx)
{
	xvattr_t xva;
	vnode_t *xattrdirvp = NULL;
	cred_t *kcr;
	xoptattr_t *xoap;
	int rc;
	reparse_vsd_t *rpv;

	if (vp->v_type == VLNK ||
	    !vfs_has_feature(vp->v_vfsp, VFSFT_XVATTR))
		return (SET_ERROR(ENOTSUP));

	kcr = zone_kcred();
	if ((rc = VOP_LOOKUP(vp, "", &xattrdirvp, NULL,
	    LOOKUP_XATTR, NULL, kcr, cctx, NULL, NULL)) != 0) {
		cmn_err(CE_WARN, "!%s: XATTR lookup failed: %d", __func__, rc);
		return (rc);
	}

	xva_init(&xva);
	xoap = xva_getxoptattr(&xva);
	ASSERT(xoap != NULL);

	XVA_SET_REQ(&xva, XAT_REPARSE);
	xoap->xoa_reparse = 0;
	XVA_SET_REQ(&xva, XAT_REPARSE_TAG);
	xoap->xoa_reparse_tag = 0;

	rpv = reparse_get_vsd(vp);
	mutex_enter(&rpv->rpv_lock);

	rc = VOP_SETATTR(vp, &xva.xva_vattr, 0, kcr, cctx);
	if (rc != 0) {
		cmn_err(CE_NOTE, "!%s: failed to clear Reparse attributes: %d",
		    __func__, rc);
		goto errout;
	}

	rc = VOP_REMOVE(xattrdirvp, "SUNWreparse", kcr, cctx, 0);
	if (rc == ENOENT)
		rc = 0;
	else if (rc != 0)
		cmn_err(CE_WARN, "!%s: failed to remove reparse data: %d",
		    __func__, rc);

	/*
	 * We've cleared the bit; clear the cached struct
	 * even if removing the xattr file failed.
	 */
	if (rpv->rpv_data != NULL) {
		reparse_free_data_locked(rpv->rpv_data);
		rpv->rpv_data = NULL;
	}

errout:
	mutex_exit(&rpv->rpv_lock);

	VN_RELE(xattrdirvp);

	return (rc);
}

static reparse_data_t *
reparse_alloc_data(reparse_vsd_t *rpv, int flags)
{
	reparse_data_t *rp;

	/* Initialized with one ref for caller */
	rp = kmem_zalloc(sizeof (*rp), flags);
	if (rp != NULL) {
		rp->rp_cnt = 1;
		rp->rp_vsd = rpv;
	}
	return (rp);
}


static void
reparse_free_data_locked(reparse_data_t *rp)
{
	ASSERT3S(rp->rp_cnt, >, 0);
	if (--rp->rp_cnt == 0) {
		nvlist_free(rp->rp_nvl);
		rp->rp_nvl = NULL;
		if (rp->rp_vsd->rpv_data == rp)
			rp->rp_vsd->rpv_data = NULL;
		kmem_free(rp, sizeof (*rp));
	}
}

void
reparse_free_data(reparse_data_t *rp)
{
	mutex_enter(&rp->rp_vsd->rpv_lock);
	reparse_free_data_locked(rp);
	mutex_exit(&rp->rp_vsd->rpv_lock);
}

static void
reparse_vsd_destroy(void *arg)
{
	reparse_vsd_t *rpv = arg;

	mutex_enter(&rpv->rpv_lock);
	if (rpv->rpv_data != NULL) {
		ASSERT3S(rpv->rpv_data->rp_cnt, ==, 1);
		reparse_free_data_locked(rpv->rpv_data);
		rpv->rpv_data = NULL;
	}
	mutex_exit(&rpv->rpv_lock);
	kmem_cache_free(reparse_vsd_cache, rpv);
}


static int
reparse_vsd_constructor(void *buf, void *cdrarg, int kmflags)
{
	reparse_vsd_t *rpv = buf;

	mutex_init(&rpv->rpv_lock, NULL, MUTEX_DEFAULT, NULL);
	rpv->rpv_data = NULL;
	return (0);
}

static void
reparse_vsd_destructor(void *buf, void *cdarg)
{
	reparse_vsd_t *rpv = buf;

	mutex_destroy(&rpv->rpv_lock);
}

void
reparse_data_init(void)
{
	vsd_create(&reparse_vsd_key, reparse_vsd_destroy);
	reparse_vsd_cache = kmem_cache_create("reparse_vsd_cache",
	    sizeof (reparse_vsd_t), 0, reparse_vsd_constructor,
	    reparse_vsd_destructor, NULL, NULL, NULL, 0);
}
