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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * Server Service RPC (SRVSVC) server-side interface definition.
 * The server service provides a remote administration interface.
 *
 * This service uses NERR/Win32 error codes rather than NT status
 * values.
 */

#include <sys/errno.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <time.h>
#include <thread.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libmlrpc/libmlrpc.h>
#include "srvsvc1.ndl"
#include "srv_fake.h"

#define	SMB_SRVSVC_MAXBUFLEN		(8 * 1024 * 1024)
#define	SMB_SRVSVC_MAXPREFLEN		((uint32_t)(-1))

typedef struct mslm_infonres srvsvc_infonres_t;
typedef struct mslm_NetConnectEnum srvsvc_NetConnectEnum_t;

static DWORD mlsvc_NetShareEnumLevel0(ndr_xa_t *, srvsvc_infonres_t *,
    smb_svcenum_t *, int);
static DWORD mlsvc_NetShareEnumLevel1(ndr_xa_t *, srvsvc_infonres_t *,
    smb_svcenum_t *, int);
static DWORD mlsvc_NetShareEnumLevel2(ndr_xa_t *, srvsvc_infonres_t *,
    smb_svcenum_t *, int);
static DWORD mlsvc_NetShareEnumCommon(ndr_xa_t *, smb_svcenum_t *,
    smb_share_t *, void *);
static boolean_t srvsvc_add_autohome(ndr_xa_t *, smb_svcenum_t *, void *);
static char *srvsvc_share_mkpath(ndr_xa_t *, char *);

static void srvsvc_estimate_limit(smb_svcenum_t *, uint32_t);

static char empty_string[1];

static ndr_stub_table_t srvsvc_stub_table[];

static ndr_service_t srvsvc_service = {
	"SRVSVC",			/* name */
	"Server services",		/* desc */
	"\\srvsvc",			/* endpoint */
	"\\PIPE\\ntsvcs",		/* sec_addr_port */
	"4b324fc8-1670-01d3-1278-5a47bf6ee188", 3,	/* abstract */
	NDR_TRANSFER_SYNTAX_UUID,		2,	/* transfer */
	0,				/* no bind_instance_size */
	0,				/* no bind_req() */
	0,				/* no unbind_and_close() */
	0,				/* use generic_call_stub() */
	&TYPEINFO(srvsvc_interface),	/* interface ti */
	srvsvc_stub_table		/* stub_table */
};

/*
 * srvsvc_initialize
 *
 * This function registers the SRVSVC RPC interface with the RPC runtime
 * library. It must be called in order to use either the client side
 * or the server side functions.
 */
void
srvsvc_initialize(void)
{
	(void) ndr_svc_register(&srvsvc_service);
}


static int
srvsvc_s_NetServerGetInfo(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetServerGetInfo *param = arg;
	struct mslm_SERVER_INFO_100 *info100;
	struct mslm_SERVER_INFO_101 *info101;
	struct mslm_SERVER_INFO_102 *info102;
	char sys_comment[SMB_PI_MAX_COMMENT];
	char hostname[NETBIOS_NAME_SZ];
	smb_version_t version;

	if (smb_getnetbiosname(hostname, sizeof (hostname)) != 0) {
netservergetinfo_no_memory:
		bzero(param, sizeof (struct mslm_NetServerGetInfo));
		return (ERROR_NOT_ENOUGH_MEMORY);
	}

	(void) smb_config_get_comment(sys_comment, sizeof (sys_comment));
	if (*sys_comment == '\0')
		(void) strcpy(sys_comment, " ");

	smb_config_get_version(&version);

	switch (param->level) {
	case 100:
		info100 = NDR_NEW(mxa, struct mslm_SERVER_INFO_100);
		if (info100 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info100, sizeof (struct mslm_SERVER_INFO_100));
		info100->sv100_platform_id = SV_PLATFORM_ID_NT;
		info100->sv100_name = (uint8_t *)NDR_STRDUP(mxa, hostname);
		if (info100->sv100_name == NULL)
			goto netservergetinfo_no_memory;

		param->result.ru.info100 = info100;
		break;

	case 101:
		info101 = NDR_NEW(mxa, struct mslm_SERVER_INFO_101);
		if (info101 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info101, sizeof (struct mslm_SERVER_INFO_101));
		info101->sv101_platform_id = SV_PLATFORM_ID_NT;
		info101->sv101_version_major = version.sv_major;
		info101->sv101_version_minor = version.sv_minor;
		info101->sv101_type = SV_TYPE_DEFAULT;
		info101->sv101_name = (uint8_t *)NDR_STRDUP(mxa, hostname);
		info101->sv101_comment
		    = (uint8_t *)NDR_STRDUP(mxa, sys_comment);

		if (info101->sv101_name == NULL ||
		    info101->sv101_comment == NULL)
			goto netservergetinfo_no_memory;

		param->result.ru.info101 = info101;
		break;

	case 102:
		info102 = NDR_NEW(mxa, struct mslm_SERVER_INFO_102);
		if (info102 == NULL)
			goto netservergetinfo_no_memory;

		bzero(info102, sizeof (struct mslm_SERVER_INFO_102));
		info102->sv102_platform_id = SV_PLATFORM_ID_NT;
		info102->sv102_version_major = version.sv_major;
		info102->sv102_version_minor = version.sv_minor;
		info102->sv102_type = SV_TYPE_DEFAULT;
		info102->sv102_name = (uint8_t *)NDR_STRDUP(mxa, hostname);
		info102->sv102_comment
		    = (uint8_t *)NDR_STRDUP(mxa, sys_comment);

		/*
		 * The following level 102 fields are defaulted to zero
		 * by virtue of the call to bzero above.
		 *
		 * sv102_users
		 * sv102_disc
		 * sv102_hidden
		 * sv102_announce
		 * sv102_anndelta
		 * sv102_licenses
		 * sv102_userpath
		 */
		if (info102->sv102_name == NULL ||
		    info102->sv102_comment == NULL)
			goto netservergetinfo_no_memory;

		param->result.ru.info102 = info102;
		break;

	default:
		bzero(&param->result,
		    sizeof (struct mslm_NetServerGetInfo_result));
		param->status = ERROR_ACCESS_DENIED;
		return (NDR_DRC_OK);
	}

	param->result.level = param->level;
	param->status = ERROR_SUCCESS;
	return (NDR_DRC_OK);
}

/*
 * srvsvc_estimate_limit
 *
 * Estimate the number of objects that will fit in prefmaxlen.
 * nlimit is adjusted here.
 */
static void
srvsvc_estimate_limit(smb_svcenum_t *se, uint32_t obj_size)
{
	DWORD max_cnt;

	if (obj_size == 0) {
		se->se_nlimit = 0;
		return;
	}

	if ((max_cnt = (se->se_prefmaxlen / obj_size)) == 0) {
		se->se_nlimit = 0;
		return;
	}

	if (se->se_ntotal > max_cnt)
		se->se_nlimit = max_cnt;
	else
		se->se_nlimit = se->se_ntotal;
}

/*
 * srvsvc_s_NetShareEnum
 *
 * Enumerate all shares (see also NetShareEnumSticky).
 *
 * Request for various levels of information about our shares.
 * Level 0: share names.
 * Level 1: share name, share type and comment field.
 * Level 2: everything that we know about the shares.
 * Level 501: level 1 + flags.
 * Level 502: level 2 + security descriptor.
 */
static int
srvsvc_s_NetShareEnum(void *arg, ndr_xa_t *mxa)
{
	struct mslm_NetShareEnum *param = arg;
	srvsvc_infonres_t *infonres;
	smb_svcenum_t se;
	DWORD status;

	infonres = NDR_NEW(mxa, srvsvc_infonres_t);
	if (infonres == NULL) {
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = ERROR_NOT_ENOUGH_MEMORY;
		return (NDR_DRC_OK);
	}

	infonres->entriesread = 0;
	infonres->entries = NULL;
	param->result.level = param->level;
	param->result.ru.bufptr0 = (void*)infonres;

	bzero(&se, sizeof (smb_svcenum_t));
	se.se_type = SMB_SVCENUM_TYPE_SHARE;
	se.se_level = param->level;
	se.se_ntotal = smb_shr_count();
	se.se_nlimit = se.se_ntotal;

	if (param->prefmaxlen == SMB_SRVSVC_MAXPREFLEN ||
	    param->prefmaxlen > SMB_SRVSVC_MAXBUFLEN)
		se.se_prefmaxlen = SMB_SRVSVC_MAXBUFLEN;
	else
		se.se_prefmaxlen = param->prefmaxlen;

	if (param->resume_handle) {
		se.se_resume = *param->resume_handle;
		se.se_nskip = se.se_resume;
		*param->resume_handle = 0;
	}

	switch (param->level) {
	case 0:
		status = mlsvc_NetShareEnumLevel0(mxa, infonres, &se, 0);
		break;

	case 1:
		status = mlsvc_NetShareEnumLevel1(mxa, infonres, &se, 0);
		break;

	case 2:
		status = mlsvc_NetShareEnumLevel2(mxa, infonres, &se, 0);
		break;

	default:
		status = ERROR_INVALID_LEVEL;
		break;
	}

	if (status != 0) {
		bzero(param, sizeof (struct mslm_NetShareEnum));
		param->status = status;
		return (NDR_DRC_OK);
	}

	if (se.se_nlimit == 0) {
		param->status = ERROR_SUCCESS;
		return (NDR_DRC_OK);
	}

	if (param->resume_handle &&
	    param->prefmaxlen != SMB_SRVSVC_MAXPREFLEN) {
		if (se.se_resume < se.se_ntotal) {
			*param->resume_handle = se.se_resume;
			status = ERROR_MORE_DATA;
		}
	}

	param->totalentries = se.se_ntotal;
	param->status = status;
	return (NDR_DRC_OK);
}


/*
 * NetShareEnum Level 0
 */
static DWORD
mlsvc_NetShareEnumLevel0(ndr_xa_t *mxa, srvsvc_infonres_t *infonres,
    smb_svcenum_t *se, int sticky)
{
	struct mslm_NetShareInfo_0 *info0;
	smb_shriter_t iterator;
	smb_share_t *si;
	DWORD status;

	srvsvc_estimate_limit(se,
	    sizeof (struct mslm_NetShareInfo_0) + MAXNAMELEN);
	if (se->se_nlimit == 0)
		return (ERROR_SUCCESS);

	info0 = NDR_NEWN(mxa, struct mslm_NetShareInfo_0, se->se_nlimit);
	if (info0 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	smb_shr_iterinit(&iterator);

	se->se_nitems = 0;
	while ((si = smb_shr_iterate(&iterator)) != NULL) {
		if (se->se_nskip > 0) {
			--se->se_nskip;
			continue;
		}

		++se->se_resume;

		if (sticky && (si->shr_flags & SMB_SHRF_TRANS))
			continue;

		if (si->shr_flags & SMB_SHRF_AUTOHOME)
			continue;

		if (se->se_nitems >= se->se_nlimit) {
			se->se_nitems = se->se_nlimit;
			break;
		}

		status = mlsvc_NetShareEnumCommon(mxa, se, si, (void *)info0);
		if (status != ERROR_SUCCESS)
			break;

		++se->se_nitems;
	}

	if (se->se_nitems < se->se_nlimit) {
		if (srvsvc_add_autohome(mxa, se, (void *)info0))
			++se->se_nitems;
	}

	infonres->entriesread = se->se_nitems;
	infonres->entries = info0;
	return (ERROR_SUCCESS);
}

/*
 * NetShareEnum Level 1
 */
static DWORD
mlsvc_NetShareEnumLevel1(ndr_xa_t *mxa, srvsvc_infonres_t *infonres,
    smb_svcenum_t *se, int sticky)
{
	struct mslm_NetShareInfo_1 *info1;
	smb_shriter_t iterator;
	smb_share_t *si;
	DWORD status;

	srvsvc_estimate_limit(se,
	    sizeof (struct mslm_NetShareInfo_1) + MAXNAMELEN);
	if (se->se_nlimit == 0)
		return (ERROR_SUCCESS);

	info1 = NDR_NEWN(mxa, struct mslm_NetShareInfo_1, se->se_nlimit);
	if (info1 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	smb_shr_iterinit(&iterator);

	se->se_nitems = 0;
	while ((si = smb_shr_iterate(&iterator)) != 0) {
		if (se->se_nskip > 0) {
			--se->se_nskip;
			continue;
		}

		++se->se_resume;

		if (sticky && (si->shr_flags & SMB_SHRF_TRANS))
			continue;

		if (si->shr_flags & SMB_SHRF_AUTOHOME)
			continue;

		if (se->se_nitems >= se->se_nlimit) {
			se->se_nitems = se->se_nlimit;
			break;
		}

		status = mlsvc_NetShareEnumCommon(mxa, se, si, (void *)info1);
		if (status != ERROR_SUCCESS)
			break;

		++se->se_nitems;
	}

	if (se->se_nitems < se->se_nlimit) {
		if (srvsvc_add_autohome(mxa, se, (void *)info1))
			++se->se_nitems;
	}

	infonres->entriesread = se->se_nitems;
	infonres->entries = info1;
	return (ERROR_SUCCESS);
}

/*
 * NetShareEnum Level 2
 */
static DWORD
mlsvc_NetShareEnumLevel2(ndr_xa_t *mxa, srvsvc_infonres_t *infonres,
    smb_svcenum_t *se, int sticky)
{
	struct mslm_NetShareInfo_2 *info2;
	smb_shriter_t iterator;
	smb_share_t *si;
	DWORD status;

	srvsvc_estimate_limit(se,
	    sizeof (struct mslm_NetShareInfo_2) + MAXNAMELEN);
	if (se->se_nlimit == 0)
		return (ERROR_SUCCESS);

	info2 = NDR_NEWN(mxa, struct mslm_NetShareInfo_2, se->se_nlimit);
	if (info2 == NULL)
		return (ERROR_NOT_ENOUGH_MEMORY);

	smb_shr_iterinit(&iterator);

	se->se_nitems = 0;
	while ((si = smb_shr_iterate(&iterator)) != 0) {
		if (se->se_nskip > 0) {
			--se->se_nskip;
			continue;
		}

		++se->se_resume;

		if (sticky && (si->shr_flags & SMB_SHRF_TRANS))
			continue;

		if (si->shr_flags & SMB_SHRF_AUTOHOME)
			continue;

		if (se->se_nitems >= se->se_nlimit) {
			se->se_nitems = se->se_nlimit;
			break;
		}

		status = mlsvc_NetShareEnumCommon(mxa, se, si, (void *)info2);
		if (status != ERROR_SUCCESS)
			break;

		++se->se_nitems;
	}

	if (se->se_nitems < se->se_nlimit) {
		if (srvsvc_add_autohome(mxa, se, (void *)info2))
			++se->se_nitems;
	}

	infonres->entriesread = se->se_nitems;
	infonres->entries = info2;
	return (ERROR_SUCCESS);
}


/*
 * mlsvc_NetShareEnumCommon
 *
 * Build the levels 0, 1, 2, 501 and 502 share information. This function
 * is called by the various NetShareEnum levels for each share. If
 * we cannot build the share data for some reason, we return an error
 * but the actual value of the error is not important to the caller.
 * The caller just needs to know not to include this info in the RPC
 * response.
 *
 * Returns:
 *	ERROR_SUCCESS
 *	ERROR_NOT_ENOUGH_MEMORY
 *	ERROR_INVALID_LEVEL
 */
static DWORD
mlsvc_NetShareEnumCommon(ndr_xa_t *mxa, smb_svcenum_t *se,
    smb_share_t *si, void *infop)
{
	struct mslm_NetShareInfo_0 *info0;
	struct mslm_NetShareInfo_1 *info1;
	struct mslm_NetShareInfo_2 *info2;
	// srvsvc_sd_t sd; // XXX
	uint8_t *netname;
	uint8_t *comment;
	uint8_t *passwd;
	uint8_t *path;
	int i = se->se_nitems;

	netname = (uint8_t *)NDR_STRDUP(mxa, si->shr_name);
	comment = (uint8_t *)NDR_STRDUP(mxa, si->shr_cmnt);
	passwd = (uint8_t *)NDR_STRDUP(mxa, empty_string);
	path = (uint8_t *)srvsvc_share_mkpath(mxa, si->shr_path);

	if (!netname || !comment || !passwd || !path)
		return (ERROR_NOT_ENOUGH_MEMORY);

	switch (se->se_level) {
	case 0:
		info0 = (struct mslm_NetShareInfo_0 *)infop;
		info0[i].shi0_netname = netname;
		break;

	case 1:
		info1 = (struct mslm_NetShareInfo_1 *)infop;
		info1[i].shi1_netname = netname;
		info1[i].shi1_comment = comment;
		info1[i].shi1_type = si->shr_type;
		break;

	case 2:
		info2 = (struct mslm_NetShareInfo_2 *)infop;
		info2[i].shi2_netname = netname;
		info2[i].shi2_comment = comment;
		info2[i].shi2_path = path;
		info2[i].shi2_type = si->shr_type;
		info2[i].shi2_permissions = 0;
		info2[i].shi2_max_uses = SHI_USES_UNLIMITED;
		info2[i].shi2_current_uses = 0;
		info2[i].shi2_passwd = passwd;
		break;

	default:
		return (ERROR_INVALID_LEVEL);
	}

	return (ERROR_SUCCESS);
}

/*
 * srvsvc_add_autohome
 *
 * Add the autohome share for the user. The share must not be a permanent
 * share to avoid duplicates.
 */
static boolean_t
srvsvc_add_autohome(ndr_xa_t *mxa, smb_svcenum_t *se, void *infop)
{
	return (B_FALSE);
}

/*
 * srvsvc_share_mkpath
 *
 * Create the share path required by the share enum calls. The path
 * is created in a heap buffer ready for use by the caller.
 *
 * Some Windows over-the-wire backup applications do not work unless a
 * drive letter is present in the share path.  We don't care about the
 * drive letter since the path is fully qualified with the volume name.
 *
 * Windows clients seem to be mostly okay with forward slashes in
 * share paths but they cannot handle one immediately after the drive
 * letter, i.e. B:/.  For consistency we convert all the slashes in
 * the path.
 *
 * Returns a pointer to a heap buffer containing the share path, which
 * could be a null pointer if the heap allocation fails.
 */
static char *
srvsvc_share_mkpath(ndr_xa_t *mxa, char *path)
{
	char tmpbuf[MAXPATHLEN];
	char *p;
	char drive_letter;

	if (strlen(path) == 0)
		return (NDR_STRDUP(mxa, path));

	drive_letter = smb_shr_drive_letter(path);
	if (drive_letter != '\0') {
		(void) snprintf(tmpbuf, MAXPATHLEN, "%c:\\", drive_letter);
		return (NDR_STRDUP(mxa, tmpbuf));
	}

	/*
	 * Strip the volume name from the path (/vol1/home -> /home).
	 */
	p = path;
	p += strspn(p, "/");
	p += strcspn(p, "/");
	p += strspn(p, "/");
	(void) snprintf(tmpbuf, MAXPATHLEN, "%c:/%s", 'B', p);
	(void) strsubst(tmpbuf, '/', '\\');

	return (NDR_STRDUP(mxa, tmpbuf));
}



static ndr_stub_table_t srvsvc_stub_table[] = {
	{ srvsvc_s_NetServerGetInfo,	SRVSVC_OPNUM_NetServerGetInfo },
	{ srvsvc_s_NetShareEnum,	SRVSVC_OPNUM_NetShareEnum },
	{0}
};
