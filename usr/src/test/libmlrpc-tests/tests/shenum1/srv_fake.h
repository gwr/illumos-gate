
#define	ERROR_SUCCESS			0
#define	ERROR_ACCESS_DENIED		5
#define	ERROR_NOT_ENOUGH_MEMORY		8
#define	ERROR_INVALID_LEVEL		124
#define	ERROR_MORE_DATA			234

#define	NETBIOS_NAME_SZ			16
#define	SMB_PI_MAX_COMMENT		58
#define	SMB_SHARE_CMNT_MAX		(64 * 4)

#define SV_TYPE_DEFAULT 		0x9003
#define	SMB_SVCENUM_TYPE_SHARE	0x53484152	/* 'SHAR' */

#define	SMB_SHRF_ADMIN		0x01000000
#define	SMB_SHRF_TRANS		0x10000000
#define	SMB_SHRF_PERM		0x20000000
#define	SMB_SHRF_AUTOHOME	0x40000000

/* Maximum uses for shiX_max_uses fields */
#define SHI_USES_UNLIMITED      (DWORD)-1

typedef struct smb_share {
	char		shr_name[MAXNAMELEN];
	char		shr_path[MAXPATHLEN];
	char		shr_cmnt[SMB_SHARE_CMNT_MAX];
	char		shr_container[MAXPATHLEN];
	uint32_t	shr_flags;
	uint32_t	shr_type;
	uint32_t	shr_refcnt;
	uint32_t	shr_access_value;	/* host return access value */
	uid_t		shr_uid;		/* autohome only */
	gid_t		shr_gid;		/* autohome only */
	char		shr_access_none[MAXPATHLEN];
	char		shr_access_ro[MAXPATHLEN];
	char		shr_access_rw[MAXPATHLEN];
	// smb_cfg_val_t	shr_encrypt;
} smb_share_t;

typedef struct smb_shriter {
	smb_share_t	si_share;
	int		si_idx;
	boolean_t	si_first;
} smb_shriter_t;

typedef struct smb_svcenum {
	uint32_t	se_type;	/* object type to enumerate */
	uint32_t	se_level;	/* level of detail being requested */
	uint32_t	se_prefmaxlen;	/* client max size buffer preference */
					/* (ignored by kernel) */
	uint32_t	se_resume;	/* client resume handle */
	uint32_t	se_bavail;	/* remaining buffer space in bytes */
	uint32_t	se_bused;	/* consumed buffer space in bytes */
	uint32_t	se_ntotal;	/* total number of objects */
	uint32_t	se_nlimit;	/* max number of objects to return */
	uint32_t	se_nitems;	/* number of objects in buf */
	uint32_t	se_nskip;	/* number of objects to skip */
	uint32_t	se_status;	/* enumeration status */
	uint32_t	se_buflen;	/* length of the buffer in bytes */
	uint8_t		se_buf[1];	/* buffer to hold enumeration data */
} smb_svcenum_t;

typedef struct smb_version {
	uint32_t	sv_size;
	uint32_t	sv_major;
	uint32_t	sv_minor;
	uint32_t	sv_build_number;
	uint32_t	sv_platform_id;
} smb_version_t;

int smb_getnetbiosname(char *buf, size_t buflen);
int smb_config_get_comment(char *cbuf, int bufsz);
void smb_config_get_version(smb_version_t *version);
char *strsubst(char *s, char orgchar, char newchar);

int smb_shr_count(void);
void smb_shr_iterinit(smb_shriter_t *);
smb_share_t *smb_shr_iterate(smb_shriter_t *);
char smb_shr_drive_letter(const char *path);
