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
 * smb_quota_add_ctrldir
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
void
smb_share_quota_add(const char *path)
{
	int newfd, dirfd, afd;
	nvlist_t *attr;
	char dir[MAXPATHLEN], file[MAXPATHLEN], *acl_text;
	acl_t *aclp, *existing_aclp;
	boolean_t qdir_created, prop_hidden = B_FALSE, prop_sys = B_FALSE;
	struct stat statbuf;

	assert(path != NULL);

	(void) snprintf(dir, MAXPATHLEN, ".%s/%s", path, SMB_QUOTA_CNTRL_DIR);
	(void) snprintf(file, MAXPATHLEN, "%s/%s", dir, SMB_QUOTA_CNTRL_FILE);
	if ((mkdir(dir, 0750) < 0) && (errno != EEXIST))
		return;
	qdir_created = (errno == EEXIST) ? B_FALSE : B_TRUE;

	if ((dirfd = open(dir, O_RDONLY)) < 0) {
		if (qdir_created)
			(void) remove(dir);
		return;
	}

	if (fgetattr(dirfd, XATTR_VIEW_READWRITE, &attr) != 0) {
		(void) close(dirfd);
		if (qdir_created)
			(void) remove(dir);
		return;
	}

	if ((nvlist_lookup_boolean_value(attr, A_HIDDEN, &prop_hidden) != 0) ||
	    (nvlist_lookup_boolean_value(attr, A_SYSTEM, &prop_sys) != 0)) {
		nvlist_free(attr);
		(void) close(dirfd);
		if (qdir_created)
			(void) remove(dir);
		return;
	}
	nvlist_free(attr);

	/*
	 * Before setting attr or acl we check if the they have already been
	 * set to what we want. If so we could be dealing with a received
	 * snapshot and setting these is not needed.
	 */

	if (!prop_hidden || !prop_sys) {
		if (nvlist_alloc(&attr, NV_UNIQUE_NAME, 0) == 0) {
			if ((nvlist_add_boolean_value(
			    attr, A_HIDDEN, 1) != 0) ||
			    (nvlist_add_boolean_value(
			    attr, A_SYSTEM, 1) != 0) ||
			    (fsetattr(dirfd, XATTR_VIEW_READWRITE, attr))) {
				nvlist_free(attr);
				(void) close(dirfd);
				if (qdir_created)
					(void) remove(dir);
				return;
			}
		}
		nvlist_free(attr);
	}

	(void) close(dirfd);

	if (stat(file, &statbuf) != 0) {
		if ((newfd = creat(file, 0640)) < 0) {
			if (qdir_created)
				(void) remove(dir);
			return;
		}
		(void) close(newfd);
	}

	afd = attropen(file, SMB_QUOTA_CNTRL_INDEX_XATTR, O_RDWR | O_CREAT,
	    0640);
	if (afd == -1) {
		(void) unlink(file);
		if (qdir_created)
			(void) remove(dir);
		return;
	}
	(void) close(afd);

	if (acl_get(file, 0, &existing_aclp) == -1) {
		(void) unlink(file);
		if (qdir_created)
			(void) remove(dir);
		return;
	}

	acl_text = acl_totext(existing_aclp, ACL_COMPACT_FMT);
	if (acl_text == NULL) {
		acl_free(existing_aclp);
		(void) unlink(file);
		if (qdir_created)
			(void) remove(dir);
		return;
	}
	acl_free(existing_aclp);

	aclp = NULL;
	if (strcmp(acl_text, SMB_QUOTA_CNTRL_PERM) != 0) {
		if (acl_fromtext(SMB_QUOTA_CNTRL_PERM, &aclp) != 0) {
			free(acl_text);
			(void) unlink(file);
			if (qdir_created)
				(void) remove(dir);
			return;
		}
		if (acl_set(file, aclp) == -1) {
			free(acl_text);
			(void) unlink(file);
			if (qdir_created)
				(void) remove(dir);
			acl_free(aclp);
			return;
		}
		acl_free(aclp);
	}
	free(acl_text);
}

/*
 * smb_quota_remove_ctrldir
 *
 * Remove SMB_QUOTA_CNTRL_FILE and SMB_QUOTA_CNTRL_DIR.
 */
void
smb_share_quota_remove(const char *path)
{
	char dir[MAXPATHLEN], file[MAXPATHLEN];
	assert(path);

	(void) snprintf(dir, MAXPATHLEN, ".%s/%s", path, SMB_QUOTA_CNTRL_DIR);
	(void) snprintf(file, MAXPATHLEN, "%s/%s", dir, SMB_QUOTA_CNTRL_FILE);
	(void) unlink(file);
	(void) remove(dir);
}
