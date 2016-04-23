
/* For use in extended attribute manipulation. */
#define FSI_GEASIZE		sizeof(FSI_GEA)
#define FSI_BIGGEASIZE(geap)	(FSI_GEASIZE + (geap)->namelen+1)

#define FSI_FEASIZE		sizeof(FSI_FEA)
/*
 * FSI_BIGFEASIZE( IN feap, OUT size ) fixed for alignment problems
 */
#define FSI_BIGFEASIZE(feap, size) {					\
	ushort	TMP_vallen;						\
	(void) memcpy( &TMP_vallen, &((feap)->vallen), sizeof( TMP_vallen ));\
	size = FSI_FEASIZE+(feap)->namelen+1+TMP_vallen; 		\
	}



/*
 * Get Extended Attribute (GEA) and GEA list
 * [MS-CIFS] 2.2.1.2.1 SMB_GEA, SMB_GEALIST
 *
 * These are just the name without the value
 * i.e. get selected EAs by name.
 */
typedef struct {
	uchar_t		namelen;	/* length of name (without '\0') */
	char		*name;		/* EA name */
} FSI_GEA;

typedef struct {
	ulong		len;		/* length of GEA list -
					 * does not include size of FSI_GEALIST
					 */
	int		cnt;		/* number of GEAs in list */
	FSI_GEA		*list;		/* array of GEAs */
} FSI_GEALIST;

/*
 * Full Extended Attribute (FEA) and FEA list
 * [MS-CIFS] 2.2.1.2.2 SMB_FEA, SMB_FEALIST
 *
 * These are name+value pairs.
 */
typedef struct {
	uchar_t		flag;		/* flag field */
	uchar_t		namelen;	/* length of name (without '\0') */
	ushort		vallen;		/* length of value */
	char		*name;		/* EA name */
	char		*value;		/* EA value */
	/* XXX implementation!	int	maxlen;	/* max length this fea has ever been */
} FSI_FEA;

typedef struct {
	ulong		len;		/* length of FEA list -
					 * does not include size of FSI_FEALIST
					 * If FSI_BufferTooSmall and not 0,
					 * then length needed.
					 */
	ulong		totlen;		/* total length of all FEAs
					 * (including those not requested)
					 * ignored on "set" operations
					 */
	int		cnt;		/* number of FEAs returned on "get" -
					 * ignored on "set" operations
					 */
	int		totcnt;		/* total number of FEAs
					 * (including those not requested)
					 * ignored on "set" operations
					 */
	FSI_FEA		*list;		/* array of FEAs */
} FSI_FEALIST;

/* XXX - implementation... */

typedef struct {
	FSI_GEALIST	*gealistp;	/* pointer to list of GEAs -
					 * ignored on "set" operations
					 */
	FSI_FEALIST	*fealistp;	/* pointer to list of FEAs -
					 * allocated by calling function
					 */
	ushort	erroffset;		/* offset of error if any */
} FSI_EAOPS;
