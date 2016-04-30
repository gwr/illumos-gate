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
 * Copyright 2016 Nexenta Systems, Inc.  All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/t_lock.h>
#include <sys/user.h>
#include <sys/uio.h>
#include <sys/pathname.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/vnode.h>

#include <sys/acl.h>
#include <sys/nbmlock.h>
#include <sys/fcntl.h>

/*
 * Are vp1 and vp2 the same vnode?
 */
int
vn_compare(vnode_t *vp1, vnode_t *vp2)
{
	vnode_t *realvp;

	if (vp1 != NULL && VOP_REALVP(vp1, &realvp, NULL) == 0)
		vp1 = realvp;
	if (vp2 != NULL && VOP_REALVP(vp2, &realvp, NULL) == 0)
		vp2 = realvp;
	return (VN_CMP(vp1, vp2));
}

/* ARGSUSED */
vfs_t *
vn_mountedvfs(vnode_t *vp)
{
	return (NULL);
}

void
xva_init(xvattr_t *xvap)
{
	bzero(xvap, sizeof (xvattr_t));
	xvap->xva_mapsize = XVA_MAPSIZE;
	xvap->xva_magic = XVA_MAGIC;
	xvap->xva_vattr.va_mask = AT_XVATTR;
	xvap->xva_rtnattrmapp = &(xvap->xva_rtnattrmap)[0];
}

/*
 * If AT_XVATTR is set, returns a pointer to the embedded xoptattr_t
 * structure.  Otherwise, returns NULL.
 */
xoptattr_t *
xva_getxoptattr(xvattr_t *xvap)
{
	xoptattr_t *xoap = NULL;
	if (xvap->xva_vattr.va_mask & AT_XVATTR)
		xoap = &xvap->xva_xoptattrs;
	return (xoap);
}

/*
 * Vnode-specific data (VSD)
 *
 * Note: As this implemention is used only by fksmbd, we make
 * the following simplifying assumptions/limitations:
 *
 * 1: Only one VSD "key" ever: vsd_key = 1
 *    and the value for it is: vp->v_vsd1
 * 2: vsd_destroy() has nothing to do, as smb nodes are
 *    always gone before the last vnode ref goes away,
 *    and it uses no destructor.
 */

static kmutex_t		vsd_lock;
uint_t	vsd_key = 1;	/* our one and only key */
uint_t	vsd_nkeys;	/* used to detect excess creates */

/*
 * Create a key (index into per vnode array)
 *	Locks out vsd_create, vsd_destroy, and vsd_free
 *	May allocate memory with lock held
 */
void
vsd_create(uint_t *keyp, void (*dtor)(void *))
{

	VERIFY(dtor == NULL);

	/*
	 * if key is allocated, do nothing
	 * duplicate calls are OK (on the same keyp)
	 */
	mutex_enter(&vsd_lock);
	if (*keyp == vsd_key) {
		mutex_exit(&vsd_lock);
		return;
	}

	/*
	 * Our one and only key is: vsd_key
	 * Make sure we only give it out once.
	 */
	VERIFY(vsd_nkeys == 0);
	vsd_nkeys++;
	*keyp = vsd_key;

	mutex_exit(&vsd_lock);
}

void
vsd_destroy(uint_t *keyp)
{
	VERIFY(*keyp == vsd_key);
	*keyp = 0;
}

/*
 * Quickly return the per vnode value that was stored with the specified key
 * Assumes the caller is protecting key from vsd_create and vsd_destroy
 * Assumes the caller is holding v_vsd_lock to protect the vsd.
 */
void *
vsd_get(vnode_t *vp, uint_t key)
{

	ASSERT(vp != NULL);
	ASSERT(mutex_owned(&vp->v_vsd_lock));

	if (key != vsd_key)
		return (NULL);

	return (vp->v_vsd1);
}

/*
 * Set a per vnode value indexed with the specified key
 * Assumes the caller is holding v_vsd_lock to protect the vsd.
 */
int
vsd_set(vnode_t *vp, uint_t key, void *value)
{

	ASSERT(vp != NULL);
	ASSERT(mutex_owned(&vp->v_vsd_lock));

	if (key != vsd_key)
		return (EINVAL);

	/*
	 * If the caller is replacing one value with another, then it is up
	 * to the caller to free/rele/destroy the previous value (if needed).
	 */
	vp->v_vsd1 = value;
	return (0);
}


#pragma init(vsd_init)
int
vsd_init(void)
{
	mutex_init(&vsd_lock, NULL, MUTEX_DEFAULT, NULL);
	return (0);
}

#pragma fini(vsd_fini)
void
vsd_fini(void)
{
	mutex_destroy(&vsd_lock);
}
