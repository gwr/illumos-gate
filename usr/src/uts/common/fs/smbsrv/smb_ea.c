
	/*
	 * Note: the first word is the size of the whole data segment,
	 * INCLUDING the size of that length word.  That means if
	 * the length word specifies a size less than four, it's
	 * invalid (and probably a client trying something fishy).
	 */


#define MS_GEASIZE	(1+1)
#define MS_FEASIZE	(1+1+2)
#define MS_FEALISTSIZE	4

static FSI_FEALIST	Fealist;
static ulong		Fealen = 0;

void
free_fealist(FSI_FEALIST *fealistp)
{
	if (fealistp && fealistp != &Fealist) {
		if (fealistp->list)
			lmfree(fealistp->list);
		lmfree(fealistp);
	}
	return;
}

void
free_eaops(FSI_EAOPS *eaopsp)
{
	if (eaopsp->gealistp && eaopsp->gealistp != FSI_ALLFEAS) {
		if (eaopsp->gealistp->list)
			lmfree(eaopsp->gealistp->list);
		lmfree(eaopsp->gealistp);
	}
	eaopsp->gealistp = NULL;

	free_fealist(eaopsp->fealistp);

	eaopsp->fealistp = NULL;

	return;
}

int
getgealist(char *dataptr, FSI_EAOPS *eaopsp)
{
	ULONG	glen;
	int	cnt = 0;
	char	*ptr;
	FSI_GEA	*geap;

	DEBUG((_lg, "getgealist()\n"));

	if (!(eaopsp->gealistp = (FSI_GEALIST *)lmcalloc(1,
		sizeof(FSI_GEALIST)))) {
		DEBUG((_lg, "\tlmcalloc(FSI_GEALIST) failed\n"));
		PutAnyError(	STATUS_INSUFF_SERVER_RESOURCES,
				ERRSRV,
				ERRnoresource
				);
		return (-1);
	}
	ptr = dataptr;
	GETDOUBLE(ptr, glen);
	ptr += MSDBLSIZE;

	if (glen < T2_EA_LEN_SZ ) {
		DEBUG((_lg, "\tglen too small (%d) - ignore the rest\n", glen));
		DEBUG((_lg, "\tno FSI_GEAs - don't bother\n"));
		lmfree(eaopsp->gealistp);
		eaopsp->gealistp = NULL;
		return (0);
        }

	glen -= T2_EA_LEN_SZ;
	while (glen > MS_GEASIZE) {
		if (!*ptr) {
			DEBUG((_lg, "\tnamelen = 0 - ignore the rest\n"));
			if (!cnt) {
				DEBUG((_lg, "\tno FSI_GEAs - don't bother\n"));
				lmfree(eaopsp->gealistp);
				eaopsp->gealistp = NULL;
				return (0);
			}
			break;
		}
		/* (*ptr + 2) --> namelen + 1 for null + 1 for len */
		glen -= (*ptr + 2);
		ptr += (*ptr + 2);
		cnt++;
	}
	/* If no geas, get them all. */
	if (!cnt) {
		DEBUG((_lg, "\tgetting all feas\n"));
		lmfree(eaopsp->gealistp);
		eaopsp->gealistp = FSI_ALLFEAS;
		return (0);
	}

	/* We have geas... fill them in. */
	eaopsp->gealistp->cnt = cnt;

	if (!(eaopsp->gealistp->list = (FSI_GEA *)lmcalloc(cnt,
		sizeof(FSI_GEA)))) {
		DEBUG((_lg, "\tlmcalloc(FSI_GEA) failed\n"));
		PutAnyError(	STATUS_INSUFF_SERVER_RESOURCES,
				ERRSRV,
				ERRnoresource
				);
		free_eaops(eaopsp);
		return (-1);
	}

	DEBUG((_lg, "\tfilling in FSI_GEALIST\n"));
	ptr = dataptr;
	ptr += MSDBLSIZE;
	glen = 0;
	for (geap = eaopsp->gealistp->list; cnt > 0; cnt--, geap++) {
		geap->namelen = *ptr++;
		geap->name = ptr;
		DEBUG((_lg, "\t%s\n", ptr));
		glen += FSI_BIGGEASIZE(geap);
		ptr += geap->namelen + 1;
	}
	eaopsp->gealistp->len = glen;

	return (0);
}

int
getfealist(char *dataptr, FSI_EAOPS *eaopsp)
{
	ULONG		totlen;
	ushort		nlen;
	ushort		vlen;
	int		cnt = 0;
	char		*ptr;
	FSI_FEA		*feap;

	DEBUG((_lg, "getfealist()\n"));

	if (!(eaopsp->fealistp = (FSI_FEALIST *)lmcalloc(1,
		sizeof(FSI_FEALIST)))) {
		DEBUG((_lg, "\tlmcalloc(FSI_FEALIST) failed\n"));
		PutAnyError(	STATUS_INSUFF_SERVER_RESOURCES,
				ERRSRV,
				ERRnoresource
				);
		return (-1);
	}

	ptr = dataptr;
	GETDOUBLE(ptr, totlen);
	ptr += MSDBLSIZE;

	DEBUG((_lg, "\ttotlen = %u\n", totlen));

	if (totlen < T2_EA_LEN_SZ){
		DEBUG((_lg, "\ttotlen too small - ignore the rest\n"));
		DEBUG((_lg, "\tno FSI_FEAs - don't bother\n"));
		lmfree(eaopsp->fealistp);
		eaopsp->fealistp = NULL;
		return (0);
	}

	totlen -= T2_EA_LEN_SZ;
	while (totlen > MS_FEASIZE) {
		ptr++;

		if (!*ptr) {
			DEBUG((_lg, "\tnamelen = 0 - ignore the rest\n"));
			if (!cnt) {
				DEBUG((_lg, "\tno FSI_FEAs - don't bother\n"));
				lmfree(eaopsp->fealistp);
				eaopsp->fealistp = NULL;
				return (0);
			}
			break;
		}

		nlen = *ptr;
		ptr++;
		DEBUG((_lg, "\tnlen = %u\n", nlen));

		GETWORD(ptr, vlen);
		ptr += MSWORDSIZE;
		DEBUG((_lg, "\tvlen = %u\n", vlen));

		ptr += nlen+1 + vlen;

		/*
		 * totlen -= (nlen+1 + vlen + 1 for flag + 1 for nlen +
		 *		2 for vlen)
		 */
		totlen -= (nlen+1 + vlen + 4);
		DEBUG((_lg, "\ttotlen = %u\n", totlen));

		cnt++;
	}

	if (!(eaopsp->fealistp->list = (FSI_FEA *)lmcalloc(cnt,
		sizeof(FSI_FEA)))) {
		DEBUG((_lg, "\tlmcalloc(FSI_FEA) failed\n"));
		PutAnyError(	STATUS_INSUFF_SERVER_RESOURCES,
				ERRSRV,
				ERRnoresource
				);
		free_eaops(eaopsp);
		return (-1);
	}

	DEBUG((_lg, "\tfilling in FSI_FEALIST\n"));
	ptr = dataptr;
	ptr += MSDBLSIZE;
	totlen = 0;
	for (feap = eaopsp->fealistp->list; cnt > 0; cnt--, feap++) {
		feap->flag = *ptr++;
		feap->namelen = *ptr++;
		GETWORD(ptr, feap->vallen);
		ptr += MSWORDSIZE;
		feap->name = ptr;
		DEBUG((_lg, "\t%s\n", ptr));
		ptr += feap->namelen + 1;
		if (feap->vallen) {
			feap->value = ptr;
			HEXDUMP("EA value", feap->value,
					(feap->vallen > 1024) ? 1024 : feap->vallen);
			ptr += feap->vallen;
		} else {
			feap->value = NULL;
		}
		
		/*
		 * alignment fix for RISC CPU's
		 */
		{
			int	size;
			FSI_BIGFEASIZE( feap, size );
			totlen += size;
		}
	}
	eaopsp->fealistp->len = totlen;

	return (0);
}

int
getfeas(FSI_PATHID *pathid, FSI_EAOPS *eaopsp)
{
	int		len;

	DEBUG((_lg, "getfeas(%s)\n", FSI_FULLPATH(pathid)));

	if (!Fealen) {
		if (!(Fealist.list = lmmalloc(Scnts->RAMRegistry.MaxEASizeInBytes))) {
			PutAnyError(	STATUS_INSUFF_SERVER_RESOURCES,
					ERRSRV,
					ERRnoresource
					);
			return (-1);
		}
		Fealist.len = Fealen = Scnts->RAMRegistry.MaxEASizeInBytes;
	}

	Fealist.len = Fealen;
	eaopsp->fealistp = &Fealist;

	if (FSI_getextattr(pathid, eaopsp) == FSI_FAILURE) {
		DEBUG((_lg, "\tFSI_getextattr() failed - errno = %d, FSI_errno = %d\n",
			errno, FSI_errno));
		if ( FSI_errno == FSI_EAsCorrupt ) {
			/*
			 * EAs corrupt, however, rather than barfing, try 
			 * to continue...
			 * PutAnyError(	STATUS_EA_CORRUPT_ERROR,
			 *		ERRDOS,
			 *		ERROR_EA_FILE_CORRUPT
			 *	  	);
			 * return (-1);
			 */
			DEBUG((_lg, "\tset len and totlen to <%d>\n",
				MS_FEALISTSIZE));
			eaopsp->fealistp->len = eaopsp->fealistp->totlen
				= MS_FEALISTSIZE;
			return (0);
		}
		if (errno == ENOMEM || FSI_errno == FSI_NoMemory) {
			PutAnyError(	STATUS_INSUFF_SERVER_RESOURCES,
					ERRSRV,
					ERRnoresource
					);
			return (-1);
		}
		if (FSI_errno == FSI_BufferTooSmall) {
			lmfree(Fealist.list);
			if (!(Fealist.list = lmmalloc(Fealist.len))) {
				Fealist.len = Fealen = 0;
				PutAnyError(
					STATUS_INSUFF_SERVER_RESOURCES,
					ERRSRV,
					ERRnoresource
					);
				return (-1);
			}
			Fealen = Fealist.len;
			DEBUG((_lg, "\tnew fealen = %lu\n", Fealen));
			if (FSI_getextattr(pathid, eaopsp) == FSI_FAILURE) {
				if (errno == ENOMEM ||
					FSI_errno == FSI_NoMemory) {
					PutAnyError(
						STATUS_INSUFF_SERVER_RESOURCES,
						ERRSRV,
						ERRnoresource
						);
				} else if ( FSI_errno == FSI_EAsCorrupt ) {
					/*
					 * this time, fail..
					 */
					PutAnyError(
						STATUS_EA_CORRUPT_ERROR,
						ERRDOS,
						ERROR_EA_FILE_CORRUPT
						);
				}
				
				return (-1);
			}
		}
		else if (FSI_errno == FSI_NotSupported) {
			DEBUG((_lg, "\tset len and totlen to <%d>\n",
				MS_FEALISTSIZE));
			eaopsp->fealistp->len = eaopsp->fealistp->totlen
				= MS_FEALISTSIZE;
			return (0);
		}
	}

	len = eaopsp->fealistp->totlen -
		(eaopsp->fealistp->totcnt * FSI_FEASIZE) +
		(eaopsp->fealistp->totcnt * MS_FEASIZE);
	eaopsp->fealistp->totlen = len + MS_FEALISTSIZE;

	len = eaopsp->fealistp->len -
		(eaopsp->fealistp->cnt * FSI_FEASIZE) +
		(eaopsp->fealistp->cnt * MS_FEASIZE);
	eaopsp->fealistp->len = len + MS_FEALISTSIZE;

	return (0);
}

/*
 * Read the nvlist (packed form) from the named stream (snode),
 * creating an nvlist (**nvlp)
 */
uint32_t
smb_ea_stream_read(smb_request_t *sr, smb_node_t *snode, nvlist_t **nvlp)
{
	smb_attr_t attr;
	iovec_t iov;
	uio_t uio;
	cred *kcr = zone_kcred();
	void *packed = NULL;
	size_t nvsize = 0;
	int rc;
	**nvlp = NULL;

	/*
	 * Get file size and buffer
	 */
	bzero(&attr, sizeof (attr));
	attr.sa_mask = SMB_AT_SIZE;
	rc = smb_node_getattr(NULL, snode, kcr, NULL, &attr);
	if (rc != 0)
		goto out;
	if (attr.sa_vattr.va_size < 8) {
		rc = EIO;
		goto out;
	}
	if (attr.sa_vattr.va_size > smb_ea_maxfsize) {
		rc = EFBIG;
		goto out;
	}
	nvsize = attr.sa_vattr.va_size;
	packed = kmem_alloc(nvsize, KM_SLEEP);

	/*
	 * Read into the buffer
	 */
	bzero(&uio, sizeof (uio));
	iov.iov_base = packed;
	iov.iov_len = nvsize;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = nvsize;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_extflg = UIO_COPY_DEFAULT;

	rc = smb_fsop_read(sr, kcr, snode, &uio);
	if (rc != 0)
		goto out;
	if (uio.uio_resid != 0) {
		rc = EIO;
		goto out;
	}

	rc = nvlist_unpack(packed, nvsize, nvlpp, KM_SLEEP);

out:
	if (packed != NULL)
		kmem_free(packed, nvsize);
	return (rc);
}

/*
 * Write the nvlist in packed form to the named stream (snode).
 * Called only with a non-empty nvlist.
 */
uint32_t
smb_ea_stream_write(smb_request_t *sr, smb_node_t *snode, nvlist_t *nvl)
{
	void *packed = NULL;
	size_t nvsize = 0;
	rc = EINVAL;
	iovec_t iov;
	uio_t uio;

	if (nvlist_size(nvl, &nvsize, NV_ENCODE_XDR) != 0)
		goto out;
	if (nvsize < 4)
		goto out;

	packed = kmem_alloc(nvsize, KM_SLEEP);
	if (nvlist_pack(nv, &packed, &nvsize, NV_ENCODE_XDR,
		    KM_SLEEP) != 0)
		goto out;

	bzero(&uio, sizeof (uio));
	iov.iov_base = packed;
	iov.iov_len = nvsize;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_resid = nvsize;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_extflg = UIO_COPY_DEFAULT;

	rc = smb_fsop_write(sr, zone_kcred(), snode, &uio);
	if (rc != 0)
		goto out;
	if (uio.uio_resid != 0) {
		rc = EIO;
		goto out;
	}

out:
	if (packed != NULL)
		kmem_free(packed, nvsize);
	return (rc);
}
