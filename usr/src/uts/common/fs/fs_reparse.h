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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2022 Tintri by DDN, Inc. All rights reserved.
 */

#ifndef _FS_REPARSE_H
#define	_FS_REPARSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/param.h>
#if defined(_KERNEL) || defined(_FAKE_KERNEL)
#include <sys/time.h>
#include <sys/nvpair.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#else
#include <libnvpair.h>
#endif

#define	FS_REPARSE_TAG_STR		"@{REPARSE"
#define	FS_REPARSE_TAG_END_CHAR		'}'
#define	FS_REPARSE_TAG_END_STR		"}"
#define	FS_TOKEN_START_STR		"@{"
#define	FS_TOKEN_END_STR		"}"

#define	REPARSED			"svc:/system/filesystem/reparse:default"
#define	MAXREPARSELEN			MAXPATHLEN
#define	REPARSED_DOOR			"/var/run/reparsed_door"
#define	REPARSED_DOORCALL_MAX_RETRY	4

/*
 * This structure is shared between kernel code and user reparsed daemon.
 * The 'res_len' must be defined as int, and not size_t, for 32-bit reparsed
 * binary and 64-bit kernel code to work together.
 */
typedef struct reparsed_door_res {
	int	res_status;
	int	res_len;
	char	res_data[1];
} reparsed_door_res_t;

/*
 * NTFS tags are 32-bit integers. ZFS prefers 64-bit attributes,
 * so that's how we define them; any value over UINT32_MAX is
 * presumptively invalid, and we use these to determine whether
 * a reparse tag has been set in the filesystem.
 */
#define	REPARSE_TAG_LEGACY	0x0
#define	REPARSE_TAG_SYMLINK	0xA000000C
#define	REPARSE_TAG_MAX_VALID	UINT32_MAX

#define	REPARSE_XATTR_MIN_SIZE	(4 + 4)	/* Vers + Reparse Tag */
/* 16k is max size of Reparse Data in NTFS. Add extra for NVLIST metadata. */
#define	REPARSE_XATTR_MAX_SIZE (REPARSE_XATTR_MIN_SIZE + 2 * 16 * 1024)

extern nvlist_t *reparse_init(void);
extern void reparse_free(nvlist_t *nvl);
extern int reparse_parse(const char *reparse_data, nvlist_t *nvl);
extern int reparse_validate(const char *reparse_data);

#if defined(_KERNEL) || defined(_FAKE_KERNEL)

typedef struct reparse_data reparse_data_t;

typedef struct reparse_vsd {
	kmutex_t rpv_lock;
	reparse_data_t *rpv_data;
} reparse_vsd_t;

struct reparse_data {
	reparse_vsd_t *rp_vsd;
	nvlist_t *rp_nvl;
	uint8_t *rp_data;
	int64_t rp_cnt;
	uint_t rp_len;
	uint32_t rp_tag;
};

extern int reparse_kderef(const char *svc_type, const char *svc_data,
			char *buf, size_t *bufsz);
extern int reparse_vnode_parse(vnode_t *vp, nvlist_t *nvl);
extern int reparse_set_data(vnode_t *, uint32_t, uint8_t *, uint_t, cred_t *,
    caller_context_t *);
extern int reparse_get_data(vnode_t *, reparse_data_t **, caller_context_t *);
extern int reparse_remove_data(vnode_t *, caller_context_t *);
extern void reparse_free_data(reparse_data_t *);
extern void reparse_data_init(void);

#else
extern int reparse_add(nvlist_t *nvl, const char *svc_type,
			const char *svc_data);
extern int reparse_remove(nvlist_t *nvl, const char *svc_type);
extern int reparse_unparse(nvlist_t *nvl, char **stringp);
extern int reparse_create(const char *path, const char *data);
extern int reparse_delete(const char *path);
extern int reparse_deref(const char *svc_type, const char *svc_data,
			char *buf, size_t *bufsz);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _FS_REPARSE_H */
