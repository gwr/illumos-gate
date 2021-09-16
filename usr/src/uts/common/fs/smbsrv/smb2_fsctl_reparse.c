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
 * Functions for manipulating Reparse Points.
 * 'set' and 'delete' are synchronized via node->n_reparse_mutex.
 */

#include <smbsrv/smb2_kproto.h>
#include <fs/fs_reparse.h>

extern caller_context_t smb_ct;

/*
 * Tags with the high bit set to 1 are 'Microsoft' reparse tags.
 * Tags with the high bit set to 0 are 'GUID' (vendor) tags.
 */
#define	IS_GUID_REPARSE(tag) ((tag & 0x80000000) == 0)

/* Reparse data must be larger than 8 and less than 16k */
#define	SMB_REPARSE_HDR_SIZE		8
#define	SMB_REPARSE_GUID_HDR_SIZE	24
#define	SMB_REPARSE_MAX_SIZE		16384
#define	SMB_REPARSE_MIN_SIZE		SMB_REPARSE_HDR_SIZE

/*
 * Gets the reparse tag on an object.
 * This is only called if XAT_REPARSE_TAG isn't present
 * on the object; set it if we get a tag.
 * If there's no reparse point or a failure occurs,
 * returns 0 (no reparse tag).
 * Caller should check if the node is a reparse point.
 */
uint32_t
smb_reparse_get_tag(smb_request_t *sr, smb_node_t *node)
{
	reparse_data_t *rp;
	smb_node_t *unnamed_node;
	uint32_t tag;
	int rc = 0;

	if (sr != NULL && !sr->sr_cfg->skc_reparse_enable)
		return (0);

	/*
	 * Checks are done on named streams, but reparse data is stored on the
	 * unnamed stream.
	 */
	unnamed_node = SMB_IS_STREAM(node);
	if (unnamed_node != NULL)
		node = unnamed_node;

	if (node->vp->v_type == VLNK) {
		tag = REPARSE_TAG_LEGACY;
	} else {
		if (reparse_get_data(node->vp, &rp, &smb_ct) == 0) {
			smb_attr_t sa = { .sa_mask = SMB_AT_REPTAG };

			tag = rp->rp_tag;
			reparse_free_data(rp);
			sa.sa_reparse_tag = tag;
			rc = smb_vop_setattr(node->vp, NULL, &sa, 0, kcred);
		} else {
			tag = 0;
		}
	}

	DTRACE_PROBE3(smb2__reparse__tag, smb_request_t *, sr, int, rc,
	    uint32_t, tag);

	return (tag);
}

uint32_t
smb_reparse_delete(smb_request_t *sr, smb_node_t *node)
{
	int rc;
	uint32_t status = NT_STATUS_SUCCESS;
	smb_node_t *unnamed_node;

	if (sr != NULL && !sr->sr_cfg->skc_reparse_enable)
		return (NT_STATUS_SUCCESS);

	/*
	 * Checks are done on named streams, but reparse data is stored on the
	 * unnamed stream.
	 */
	unnamed_node = SMB_IS_STREAM(node);
	if (unnamed_node != NULL)
		node = unnamed_node;

	mutex_enter(&node->n_reparse_mutex);

	if (!smb_node_is_reparse(node))
		goto out;

	rc = reparse_remove_data(node->vp, &smb_ct);

	if (rc == 0) {
		mutex_enter(&node->n_mutex);
		node->flags &= ~NODE_FLAGS_REPARSE;
		mutex_exit(&node->n_mutex);
	} else {
		status = smb_errno2status(rc);
	}

out:
	mutex_exit(&node->n_reparse_mutex);
	return (status);
}

/* FSCTL_GET_REPARSE_POINT */
uint32_t
smb2_fsctl_get_reparse(smb_request_t *sr, smb_fsctl_t *fsctl)
{
	reparse_data_t *rp = NULL;
	smb_ofile_t *ofile = sr->fid_ofile;
	smb_node_t *node = ofile->f_node;
	smb_node_t *unnamed_node;
	uint32_t outsize = fsctl->MaxOutputResp;
	uint32_t status = NT_STATUS_SUCCESS;
	int rc;
	uint16_t datalen;

	if (!SMB_TREE_SUPPORTS_REPARSE(sr)) {
		status = NT_STATUS_INVALID_DEVICE_REQUEST;
		goto out;
	}

	/*
	 * Oddly, Windows allows Reparse Points to be read without
	 * READ_ATTRIBUTE or READ_DATA access.
	 */

	/*
	 * Checks are done on named streams, but reparse data is stored on the
	 * unnamed stream.
	 */
	unnamed_node = SMB_IS_STREAM(node);
	if (unnamed_node != NULL)
		node = unnamed_node;

	if (!smb_node_is_reparse(node)) {
		status = NT_STATUS_NOT_A_REPARSE_POINT;
		goto out;
	}

	if ((rc = reparse_get_data(node->vp, &rp, &smb_ct)) != 0) {
		status = smb_errno2status(rc);
		goto out;
	}

	/* We don't know how to properly represent non-SMB reparse points OTW */
	if (rp->rp_tag != REPARSE_TAG_SYMLINK) {
		status = NT_STATUS_ACCESS_DENIED;
		goto out;
	}

	if (rp->rp_len > (outsize - SMB_REPARSE_HDR_SIZE)) {
		datalen = outsize - SMB_REPARSE_HDR_SIZE;
		status = NT_STATUS_BUFFER_OVERFLOW;
		/* Only encode enough to fill the output buffer */
	} else {
		datalen = rp->rp_len;
	}

	rc = smb_mbc_encodef(fsctl->out_mbc, "lw2.",
	    rp->rp_tag,		/* l */
	    rp->rp_len);	/* w2. */

	if (rc != 0) {
		status = NT_STATUS_BUFFER_TOO_SMALL;
		goto out;
	}

	if (smb_mbc_encodef(fsctl->out_mbc, "#c", datalen, rp->rp_data) != 0)
		status = NT_STATUS_INTERNAL_ERROR;

out:
	DTRACE_PROBE3(smb2__reparse__get, smb_request_t *, sr, uint32_t, status,
	    reparse_data_t *, rp);

	reparse_free_data(rp);
	return (status);
}

/* FSCTL_SET_REPARSE_POINT */
uint32_t
smb2_fsctl_set_reparse(smb_request_t *sr, smb_fsctl_t *fsctl)
{
	reparse_data_t *rp = NULL;
	smb_attr_t attr;
	smb_ofile_t *ofile = sr->fid_ofile;
	smb_node_t *node = ofile->f_node;
	smb_node_t *unnamed_node;
	cred_t *kcr = zone_kcred();
	uint8_t *data = NULL;
	uint32_t amask, tag, status = NT_STATUS_SUCCESS;
	uint32_t insize = fsctl->InputCount;
	int rc;
	uint16_t datalen = 0;

	if (!SMB_TREE_SUPPORTS_REPARSE(sr)) {
		status = NT_STATUS_INVALID_DEVICE_REQUEST;
		goto out;
	}

	amask = FILE_WRITE_ATTRIBUTES | FILE_WRITE_DATA;
	if ((ofile->f_granted_access & amask) == 0) {
		status = NT_STATUS_ACCESS_DENIED;
		goto out;
	}

	if (insize < SMB_REPARSE_MIN_SIZE || insize > SMB_REPARSE_MAX_SIZE) {
		status = NT_STATUS_IO_REPARSE_DATA_INVALID;
		goto out;
	}

	rc = smb_mbc_decodef(fsctl->in_mbc, "lw2.",
	    &tag, &datalen);
	if (rc != 0) {
		status = NT_STATUS_BUFFER_TOO_SMALL;
		goto out;
	}

	/*
	 * The length of the reparse data in the request must be the size of the
	 * input buffer less the size of the header
	 * (8 for REPARSE_DATA_BUFFER, 24 for REPARSE_GUID_DATA_BUFFER).
	 */
	if (insize != (datalen + SMB_REPARSE_HDR_SIZE) &&
	    insize != (datalen + SMB_REPARSE_GUID_HDR_SIZE)) {
		status = NT_STATUS_IO_REPARSE_DATA_INVALID;
		goto out;
	}

	/*
	 * [MS-SMB2] 3.3.5.15.13 "Handling a Set Reparse Point Request"
	 * Microsoft requires that users be a member of the Administrators group
	 * to set tags other than TAG_SYMLINK. We don't support any such
	 * tags, so just deny all of them.
	 */
	if (tag != REPARSE_TAG_SYMLINK) {
		status = NT_STATUS_ACCESS_DENIED;
		goto out;
	}

	data = kmem_alloc(datalen, KM_SLEEP);
	rc = smb_mbc_decodef(fsctl->in_mbc, "#c", datalen, data);
	if (rc != 0) {
		status = NT_STATUS_INTERNAL_ERROR;
		goto out;
	}

	if (smb_node_is_dir(node) &&
	    smb_rmdir_possible(node) != NT_STATUS_SUCCESS) {
		status = NT_STATUS_DIRECTORY_NOT_EMPTY;
		goto out;
	}

	if (smb_node_is_file(node)) {
		bzero(&attr, sizeof (attr));
		attr.sa_mask = SMB_AT_SIZE;
		rc = smb_node_getattr(sr, node, kcr, ofile, &attr);
		if (rc != 0) {
			status = smb_errno2status(rc);
			goto out;
		}
		if (attr.sa_vattr.va_size != 0) {
			status = NT_STATUS_IO_REPARSE_DATA_INVALID;
			goto out;
		}
	}

	/*
	 * Checks are done on named streams, but reparse data is stored on the
	 * unnamed stream.
	 */
	unnamed_node = SMB_IS_STREAM(node);
	if (unnamed_node != NULL)
		node = unnamed_node;

	rc = reparse_get_data(node->vp, &rp, &smb_ct);
	if (rc == 0) {
		uint32_t rp_tag = rp->rp_tag;

		reparse_free_data(rp);
		if (tag != rp_tag) {
			status = NT_STATUS_IO_REPARSE_TAG_MISMATCH;
			goto out;
		}
	} else if (rc != ENOENT) {
		status = smb_errno2status(rc);
		goto out;
	}

	mutex_enter(&node->n_reparse_mutex);
	rc = reparse_set_data(node->vp, tag, data, datalen, sr->user_cr,
	    &smb_ct);
	if (rc == 0) {
		mutex_enter(&node->n_mutex);
		node->flags |= NODE_FLAGS_REPARSE;
		mutex_exit(&node->n_mutex);
	}
	mutex_exit(&node->n_reparse_mutex);

	if (rc != 0)
		status = smb_errno2status(rc);

out:
	DTRACE_PROBE5(smb2__reparse__set, smb_request_t *, sr, uint32_t, status,
	    uint32_t, tag, char *, data, size_t, datalen);
	if (data != NULL)
		kmem_free(data, datalen);
	return (status);
}

/* FSCTL_DELETE_REPARSE_POINT */
uint32_t
smb2_fsctl_del_reparse(smb_request_t *sr, smb_fsctl_t *fsctl)
{
	reparse_data_t *rp;
	smb_ofile_t *ofile = sr->fid_ofile;
	smb_node_t *node = ofile->f_node;
	smb_node_t *unnamed_node;
	uint32_t amask, tag, status = NT_STATUS_SUCCESS;
	int rc;

	if (!SMB_TREE_SUPPORTS_REPARSE(sr)) {
		status = NT_STATUS_INVALID_DEVICE_REQUEST;
		goto out;
	}

	amask = FILE_WRITE_ATTRIBUTES | FILE_WRITE_DATA;
	if ((ofile->f_granted_access & amask) == 0) {
		status = NT_STATUS_ACCESS_DENIED;
		goto out;
	}

	/*
	 * Checks are done on named streams, but reparse data is stored on the
	 * unnamed stream.
	 */
	unnamed_node = SMB_IS_STREAM(node);
	if (unnamed_node != NULL)
		node = unnamed_node;

	if (!smb_node_is_reparse(node)) {
		status = NT_STATUS_NOT_A_REPARSE_POINT;
		goto out;
	}

	if (smb_mbc_decodef(fsctl->in_mbc, "l", &tag) != 0) {
		status = NT_STATUS_BUFFER_TOO_SMALL;
		goto out;
	}

	mutex_enter(&node->n_reparse_mutex);

	rc = reparse_get_data(node->vp, &rp, &smb_ct);

	if (rc == 0) {
		uint32_t rp_tag = rp->rp_tag;

		reparse_free_data(rp);
		if (tag != rp_tag) {
			mutex_exit(&node->n_reparse_mutex);
			status = NT_STATUS_IO_REPARSE_TAG_MISMATCH;
			goto out;
		}
		rc = reparse_remove_data(node->vp, &smb_ct);
	} else if (rc == ENOENT) {
		/* There's no reparse point to delete. */
		rc = 0;
	}

	if (rc == 0) {
		mutex_enter(&node->n_mutex);
		node->flags &= ~NODE_FLAGS_REPARSE;
		mutex_exit(&node->n_mutex);
	} else {
		status = smb_errno2status(rc);
	}
	mutex_exit(&node->n_reparse_mutex);

out:
	DTRACE_PROBE3(smb2__reparse__delete, smb_request_t *, sr,
	    uint32_t, status, reparse_data_t *, rp);

	return (status);
}

#define	SMB_SYMLINK_ERROR_TAG	0x4c4d5953 /* 'LMYS' */
#define	SMB_SYMLINK_ERROR_SIZE	(SMB_REPARSE_MAX_SIZE + 16) /* 16 byte header */

uint32_t
smb_reparse_get_error_data(smb_request_t *sr, smb_node_t *node, char *path)
{
	reparse_data_t *rp = NULL;
	smb_node_t *unnamed_node;
	size_t otw_bytes;
	uint32_t status;
	int rc = 0;

	if (!SMB_TREE_SUPPORTS_REPARSE(sr)) {
		status = NT_STATUS_INVALID_DEVICE_REQUEST;
		goto out;
	}

	/*
	 * Checks are done on named streams, but reparse data is stored on the
	 * unnamed stream.
	 */
	unnamed_node = SMB_IS_STREAM(node);
	if (unnamed_node != NULL)
		node = unnamed_node;

	if (smb_node_is_dfslink(node)) {
		status = NT_STATUS_PATH_NOT_COVERED;
		goto out;
	}

	if (reparse_get_data(node->vp, &rp, &smb_ct) != 0) {
		status = NT_STATUS_INTERNAL_ERROR;
		goto out;
	}

	if (rp->rp_tag != REPARSE_TAG_SYMLINK) {
		status = NT_STATUS_REPARSE;
		goto out;
	}

	if (sr->session->dialect >= SMB_VERS_2_BASE && path != NULL) {
		/*
		 * The 'unparsed length' in the error data is the length of the
		 * unparsed data in the OTW buffer (UTF-16).
		 * We've converted that to UTF-8 for internal processing.
		 * Hopefully that's a symmetrical operation.
		 */
		otw_bytes = smb_wcequiv_strlen(path);
		if (otw_bytes == (size_t)-1) {
			status = NT_STATUS_INTERNAL_ERROR;
			goto out;
		}

		MBC_FLUSH(&sr->raw_data);
		sr->raw_data.max_bytes = SMB_SYMLINK_ERROR_SIZE;
		rc = smb_mbc_encodef(&sr->raw_data, "lllww#c",
		    rp->rp_len + 16,		/* l */
		    SMB_SYMLINK_ERROR_TAG,	/* l */
		    rp->rp_tag,			/* l */
		    rp->rp_len,			/* w */
		    otw_bytes,			/* w */
		    rp->rp_len,			/* # */
		    rp->rp_data);		/* c */
	}

	if (rc == 0)
		status = NT_STATUS_STOPPED_ON_SYMLINK;
	else
		status = NT_STATUS_INTERNAL_ERROR;

out:
	DTRACE_PROBE3(smb2__reparse__error__data, smb_request_t *, sr, uint32_t,
	    status, reparse_data_t *, rp);

	reparse_free_data(rp);
	return (status);
}
