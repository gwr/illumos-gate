/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2018 Nexenta Systems, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <attr.h>
#include <unistd.h>
#include <libuutil.h>
#include <libzfs.h>
#include <assert.h>
#include <stddef.h>
#include <strings.h>
#include <errno.h>
#include <synch.h>

#include <sys/acl.h>
#include <sys/stat.h>

#include "libshare_smb.h"

/*
 * smb_quota "control" directory add/remove
 */

/*
 * In order to display the quota properties tab, windows clients
 * check for the existence of the quota control file.
 */
#define	SMB_QUOTA_CNTRL_DIR		".$EXTEND"
#define	SMB_QUOTA_CNTRL_FILE		"$QUOTA"
#define	SMB_QUOTA_CNTRL_INDEX_XATTR	"SUNWsmb:$Q:$INDEX_ALLOCATION"
/*
 * Note: this line needs to have the same format as what acl_totext() returns.
 */
#define	SMB_QUOTA_CNTRL_PERM		"everyone@:rw-p--aARWc--s:-------:allow"

/*
 * smb_share_quota_add
 *
 * In order to display the quota properties tab, windows clients
 * check for the existence of the quota control file, created
 * here as follows:
 * - Create SMB_QUOTA_CNTRL_DIR directory (with A_HIDDEN & A_SYSTEM
 *   attributes).
 * - Create the SMB_QUOTA_CNTRL_FILE file (with extended attribute
 *   SMB_QUOTA_CNTRL_INDEX_XATTR) in the SMB_QUOTA_CNTRL_DIR directory.
 * - Set the acl of SMB_QUOTA_CNTRL_FILE file to SMB_QUOTA_CNTRL_PERM.
 */
int
smb_share_quota_add(const char *path)
{
	int shrfd = -1;
	int dirfd = -1;
	int filefd = -1;
	int xafd = -1;
	int rc = 0;
	nvlist_t *attr = NULL;
	acl_t *aclp;
	boolean_t qdir_created = B_FALSE;
	boolean_t qfile_created = B_FALSE;
	boolean_t attr_ok = B_FALSE;
	boolean_t acl_ok = B_FALSE;

	/*
	 * Using openat etc below, so get an FD on the
	 * path at the top of the share (or bail out)
	 */
	if ((shrfd = open(path, O_RDONLY)) < 0)
		return (errno);

	/*
	 * Open the quota control dir (create if necessary)
	 */
	dirfd = openat(shrfd, SMB_QUOTA_CNTRL_DIR, O_RDONLY);
	if (dirfd < 0 && errno == ENOENT) {
		(void) mkdirat(shrfd, SMB_QUOTA_CNTRL_DIR, 0750);
		qdir_created = B_TRUE;
		dirfd = openat(shrfd, SMB_QUOTA_CNTRL_DIR, O_RDONLY);
	}
	if (dirfd < 0) {
		rc = errno;
		goto errout;
	}

	/*
	 * Set the quota control dir attributes "hidden, system"
	 *
	 * Before setting attr or acl we check if the they have already been
	 * set to what we want. If so we could be dealing with a received
	 * snapshot and setting these is not needed.
	 */
	if (!qdir_created) {
		boolean_t h = B_FALSE;
		boolean_t sys = B_FALSE;
		if (fgetattr(dirfd, XATTR_VIEW_READWRITE, &attr) == 0 &&
		    nvlist_lookup_boolean_value(attr, A_SYSTEM, &sys) == 0 &&
		    nvlist_lookup_boolean_value(attr, A_HIDDEN, &h) == 0 &&
		    h == B_TRUE && sys == B_TRUE)
			attr_ok = B_TRUE;
		nvlist_free(attr);
		attr = NULL;
	}
	if (!attr_ok) {
		if (nvlist_alloc(&attr, NV_UNIQUE_NAME, 0) != 0) {
			rc = ENOMEM;
			goto errout;
		}
		if ((nvlist_add_boolean_value(attr, A_HIDDEN, 1) != 0) ||
		    (nvlist_add_boolean_value(attr, A_SYSTEM, 1) != 0) ||
		    (fsetattr(dirfd, XATTR_VIEW_READWRITE, attr) != 0)) {
			rc = errno;
			nvlist_free(attr);
			goto errout;
		}
		nvlist_free(attr);
		attr = NULL;
	}

	/*
	 * Open the quota control file (create if necessary)
	 */
	filefd = openat(dirfd, SMB_QUOTA_CNTRL_FILE, O_RDWR, 0640);
	if (filefd < 0 && errno == ENOENT) {
		qfile_created = B_TRUE;
		filefd = openat(dirfd, SMB_QUOTA_CNTRL_FILE,
			       O_RDWR | O_CREAT, 0640);
	}
	if (filefd < 0) {
		rc = errno;
		goto errout;
	}

	/*
	 * Create the XATTR name under the quota file
	 */
	xafd = openat(filefd, SMB_QUOTA_CNTRL_INDEX_XATTR,
	    O_RDWR | O_CREAT | O_XATTR, 0640);
	if (xafd < 0) {
		rc = errno;
		goto errout;
	}
	close(xafd);

	/*
	 * Set the ACL on the quota file (if not already set)
	 */
	if (!qfile_created) {
		char *acl_text = NULL;
		aclp = NULL;
		if (facl_get(filefd, 0, &aclp) == 0 &&
		    (acl_text = acl_totext(aclp, ACL_COMPACT_FMT)) != NULL &&
		    strcmp(acl_text, SMB_QUOTA_CNTRL_PERM) == 0)
			acl_ok = B_TRUE;

		free(acl_text);
		acl_free(aclp);
	}
	if (!acl_ok) {
		aclp = NULL;
		if (acl_fromtext(SMB_QUOTA_CNTRL_PERM, &aclp) != 0) {
			rc = EINVAL;
			goto errout;
		}
		if (facl_set(filefd, aclp) < 0) {
			rc = errno;
			acl_free(aclp);
			goto errout;
		}
		acl_free(aclp);
	}

	if (rc != 0) {
	errout:	/* try to undo anythign we (may have) done */
		if (qfile_created && dirfd != -1)
			(void) unlinkat(dirfd, SMB_QUOTA_CNTRL_FILE, 0);
		if (qdir_created && shrfd != -1)
			(void) unlinkat(shrfd, SMB_QUOTA_CNTRL_DIR,
			    AT_REMOVEDIR);
	}

	/* close FDs we opened */
	if (filefd != -1)
		close(filefd);
	if (dirfd != -1)
		close(dirfd);
	if (shrfd != -1)
		close(shrfd);

	return (rc);
}

/*
 * smb_share_quota_remove
 *
 * Remove SMB_QUOTA_CNTRL_FILE and SMB_QUOTA_CNTRL_DIR.
 */
int
smb_share_quota_remove(const char *path)
{
	int fd;
	const char *qcf = SMB_QUOTA_CNTRL_DIR "/" SMB_QUOTA_CNTRL_FILE;
	const char *qcd = SMB_QUOTA_CNTRL_DIR;

	if ((fd = open(path, O_RDONLY)) < 0)
		return (errno);

	(void) unlinkat(fd, qcf, 0);
	(void) unlinkat(fd, qcd, AT_REMOVEDIR);

	close(fd);
	return (0);
}
