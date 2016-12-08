/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * Copyright (c) 2013 RackTop Systems.
 *
 * Copyright 2016 Gordon W. Ross
 */

/*
 * Get values for things that were historically constants in userdefs.h
 * i.e. DEFRID, DEFSHL
 *
 * Several things copied or moved from:
 * $SRC/cmd/oamuser/user/userdefs.c
 */

/*LINTLIBRARY*/
#define	_USERDEFS_INTERNAL 1

#include	<sys/types.h>
#include	<stdio.h>
#include	<string.h>
#include	<userdefs.h>
#include	<user_attr.h>
#include	<limits.h>
#include	<stdlib.h>
#include	<stddef.h>
#include	<time.h>
#include	<unistd.h>

#define	STR_SZ	512
#define	SKIPWS(ptr)	while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++

static char *zap_nl(char *);
static int fwrite_defs(FILE *, struct userdefs *, char *, ptrdiff_t);

static char user_defgname[STR_SZ]  = DEFGNAME;
static char user_defparent[STR_SZ] = DEFPARENT;
static char user_defskel[STR_SZ]   = DEFSKL;
static char user_defshell[STR_SZ]  = DEFSHL;
static char user_defexpire[STR_SZ] = DEFEXPIRE;
static char user_defauth[STR_SZ]   = DEFAUTH;
static char user_defprof[STR_SZ]   = DEFPROF;
static char user_defrole[STR_SZ]   = DEFROLE;
static char user_defprojname[STR_SZ] = DEFPROJNAME;
static char user_deflimpriv[STR_SZ]  = DEFLIMPRIV;
static char user_defdfltpriv[STR_SZ] = DEFDFLTPRIV;
static char user_deflock_a_r[STR_SZ] = DEFLOCK_AFTER_RETRIES;

static struct userdefs userdefs = {
	DEFRID,
	DEFGROUP,
	user_defgname,
	user_defparent,
	user_defskel,
	user_defshell,
	DEFINACT,
	user_defexpire,
	user_defauth,
	user_defprof,
	user_defrole,
	DEFPROJ,
	user_defprojname,
	user_deflimpriv,
	user_defdfltpriv,
	user_deflock_a_r
};

static char role_defgname[STR_SZ]  = DEFGNAME;
static char role_defparent[STR_SZ] = DEFPARENT;
static char role_defskel[STR_SZ]   = DEFSKL;
static char role_defshell[STR_SZ]  = DEFROLESHL;	/* role! */
static char role_defexpire[STR_SZ] = DEFEXPIRE;
static char role_defauth[STR_SZ]   = DEFAUTH;
static char role_defprof[STR_SZ]   = DEFROLEPROF;	/* role! */
static char role_defprojname[STR_SZ] = DEFPROJNAME;
static char role_deflimpriv[STR_SZ]  = DEFLIMPRIV;
static char role_defdfltpriv[STR_SZ] = DEFDFLTPRIV;
static char role_deflock_a_r[STR_SZ] = DEFLOCK_AFTER_RETRIES;

static struct userdefs roledefs = {
	DEFRID,
	DEFGROUP,
	role_defgname,
	role_defparent,
	role_defskel,
	role_defshell,
	DEFINACT,
	role_defexpire,
	role_defauth,
	role_defprof,
	"",		/* not changeable */
	DEFPROJ,
	role_defprojname,
	role_deflimpriv,
	role_defdfltpriv,
	role_deflock_a_r
};

#define	INT	0
#define	STR	1
#define	PROJID	2

#define	DEFOFF(field)		offsetof(struct userdefs, field)
#define	FIELD(up, pe, type)	(*(type *)((char *)(up) + (pe)->off))

typedef struct parsent {
	const char *name;	/* deffoo= */
	const size_t nmsz;	/* length of def= string (excluding \0) */
	const int type;		/* type of entry */
	const ptrdiff_t off;	/* offset in userdefs structure */
	const char *uakey;	/* user_attr key, if defined */
} parsent_t;

/* BEGIN CSTYLED */
static const parsent_t tab[] = {
	{ RIDSTR,	sizeof (RIDSTR) - 1,	INT,	DEFOFF(defrid) },	/* DEFRID */
	{ GIDSTR,	sizeof (GIDSTR) - 1,	INT,	DEFOFF(defgroup) },	/* DEFGROUP */
	{ GNAMSTR,	sizeof (GNAMSTR) - 1,	STR,	DEFOFF(defgname) },	/* DEFGNAME */
	{ PARSTR,	sizeof (PARSTR) - 1,	STR,	DEFOFF(defparent) },	/* DEFPARENT */
	{ SKLSTR,	sizeof (SKLSTR) - 1,	STR,	DEFOFF(defskel) },	/* DEFSKL */
	{ SHELLSTR,	sizeof (SHELLSTR) - 1,	STR,	DEFOFF(defshell) },	/* DEFSHL, DEFROLESHL */
	{ INACTSTR,	sizeof (INACTSTR) - 1,	INT,	DEFOFF(definact) },	/* DEFINACT */
	{ EXPIRESTR,	sizeof (EXPIRESTR) - 1,	STR,	DEFOFF(defexpire) },	/* DEFEXPIRE */
	{ AUTHSTR,	sizeof (AUTHSTR) - 1,	STR,	DEFOFF(defauth),	/* DEFAUTH */
		USERATTR_AUTHS_KW },
	{ PROFSTR,	sizeof (PROFSTR) - 1,	STR,	DEFOFF(defprof),	/* DEFPROF, DEFROLEPROF */
		USERATTR_PROFILES_KW },
	{ ROLESTR,	sizeof (ROLESTR) - 1,	STR,	DEFOFF(defrole),	/* DEFROLE */
		USERATTR_ROLES_KW },
	{ PROJSTR,	sizeof (PROJSTR) - 1,	PROJID,	DEFOFF(defproj) },	/* DEFPROJ */
	{ PROJNMSTR,	sizeof (PROJNMSTR) - 1,	STR,	DEFOFF(defprojname) },	/* DEFPROJNAME */
	{ LIMPRSTR,	sizeof (LIMPRSTR) - 1,	STR,	DEFOFF(deflimpriv),	/* DEFLIMPRIV */
		USERATTR_LIMPRIV_KW },
	{ DFLTPRSTR,	sizeof (DFLTPRSTR) - 1,	STR,	DEFOFF(defdfltpriv),	/* DEFDFLTPRIV */
		USERATTR_DFLTPRIV_KW },
	{ LOCK_AFTER_RETRIESSTR,	sizeof (LOCK_AFTER_RETRIESSTR) - 1,	/* DEFLOCK_AFTER_RETRIES */
		STR,	DEFOFF(deflock_after_retries),
		USERATTR_LOCK_AFTER_RETRIES_KW },
};
/* END CSTYLED */

#define	NDEF	(sizeof (tab) / sizeof (parsent_t))

static const parsent_t *
scan(char **start_p)
{
	static int ind = NDEF - 1;
	char *cur_p = *start_p;
	int lastind = ind;

	if (!*cur_p || *cur_p == '\n' || *cur_p == '#')
		return (NULL);

	/*
	 * The magic in this loop is remembering the last index when
	 * reentering the function; the entries above are also used to
	 * order the output to the default file.
	 */
	do {
		ind++;
		ind %= NDEF;

		if (strncmp(cur_p, tab[ind].name, tab[ind].nmsz) == 0) {
			*start_p = cur_p + tab[ind].nmsz;
			return (&tab[ind]);
		}
	} while (ind != lastind);

	return (NULL);
}

/*
 * getusrdef - access the user defaults file.  If it doesn't exist,
 *		then returns default values of (values in userdefs.h):
 *		defrid = 100
 *		defgroup = 1
 *		defgname = other
 *		defparent = /home
 *		defskel	= /usr/sadm/skel
 *		defshell = /bin/sh
 *		definact = 0
 *		defexpire = 0
 *		defauth = 0
 *		defprof = 0
 *		defrole = 0
 *
 *	If getusrdef() is unable to access the defaults file, it
 *	returns a NULL pointer.
 *
 * 	If user defaults file exists, then getusrdef uses values
 *	in it to override the above values.
 *
 *	Note that the userdefs_loaded, roledefs_loaded flags are
 *	more than an optimization.  Once we've return the struct
 *	to the caller, they may change any of the string members
 *	with pointers to constant strings etc.  If we were to run
 *	fread_defs() after that, it could segv trying to copy the
 *	strings from the scanner onto those constant strings.
 */

static int roledefs_loaded = 0;

struct userdefs *
_get_roledefs()
{
	FILE *fp;

	if (roledefs_loaded == 0) {

		fp = fopen(ODEFROLEFILE, "r");
		if (fp != NULL) {
			fread_defs(fp, &roledefs, B_TRUE);
			(void) fclose(fp);
		}

		fp = fopen(DEFROLEFILE, "r");
		if (fp != NULL) {
			fread_defs(fp, &roledefs, B_TRUE);
			(void) fclose(fp);
		}

		roledefs_loaded = 1;
	}
	return (&roledefs);
}

static int userdefs_loaded = 0;

struct userdefs *
_get_userdefs()
{
	FILE *fp;

	if (userdefs_loaded == 0) {

		fp = fopen(ODEFFILE, "r");
		if (fp != NULL) {
			fread_defs(fp, &userdefs, B_FALSE);
			(void) fclose(fp);
		}

		fp = fopen(DEFFILE, "r");
		if (fp != NULL) {
			fread_defs(fp, &userdefs, B_FALSE);
			(void) fclose(fp);
		}

		userdefs_loaded = 1;
	}
	return (&userdefs);
}

void
fread_defs(FILE *fp, struct userdefs *ud, boolean_t role)
{
	char instr[STR_SZ], *ptr;
	const parsent_t *pe;

	while (fgets(instr, sizeof (instr), fp) != NULL) {
		ptr = instr;

		SKIPWS(ptr);

		if (*ptr == '#')
			continue;

		pe = scan(&ptr);

		if (pe != NULL) {
			/*
			 * If reading a role file, should not see defrole,
			 * but in case we do, just skip it.
			 */
			if (role && pe->off == DEFOFF(defrole))
				continue;

			switch (pe->type) {
			case INT:
				FIELD(ud, pe, int) =
				    (int)strtol(ptr, NULL, 10);
				break;
			case PROJID:
				FIELD(ud, pe, projid_t) =
				    (projid_t)strtol(ptr, NULL, 10);
				break;
			case STR:
				/*
				 * Copy into static storage here (avoiding
				 * strdup) so _get_userdefs() doesn't leak.
				 * In here we know the userdefs struct has
				 * all STR struct members pointing to our
				 * static buffers of size STR_SZ.
				 */
				(void) strlcpy(FIELD(ud, pe, char *),
				    zap_nl(ptr), STR_SZ);
				break;
			}
		}
	}
}

static char *
zap_nl(char *s)
{
	char *p = strchr(s, '\n');
	if (p != NULL)
		*p = '\0';

	return (s);
}

extern int
fwrite_roledefs(struct __FILE *fp, struct userdefs *defs)
{
	ptrdiff_t skip;
	char *hdr;

	/* This is a role, so we must skip the defrole field */
	skip = offsetof(struct userdefs, defrole);
	hdr = FHEADER_ROLE;

	return (fwrite_defs(fp, defs, hdr, skip));
}

extern int
fwrite_userdefs(struct __FILE *fp, struct userdefs *defs)
{
	ptrdiff_t skip;
	char *hdr;

	skip = -1;
	hdr = FHEADER;

	return (fwrite_defs(fp, defs, hdr, skip));
}

/*
 * fwrite_defs
 * 	changes default values in defadduser file
 * Returns:
 *  <= 0: error
 * > 0: success
 */
static int
fwrite_defs(FILE *fp, struct userdefs *defs, char *hdr, ptrdiff_t skip)
{
	time_t timeval;		/* time value from time */
	int i, res;

	/*
	 * file format is:
	 * #<tab>Default values for adduser.  Changed mm/dd/yy hh:mm:ss.
	 * defgroup=m	(m=default group id)
	 * defgname=str1	(str1=default group name)
	 * defparent=str2	(str2=default base directory)
	 * definactive=x	(x=default inactive)
	 * defexpire=y		(y=default expire)
	 * defproj=z		(z=numeric project id)
	 * defprojname=str3	(str3=default project name)
	 * ... etc ...
	 */

	/* get time */
	timeval = time(NULL);

	/* write it to file */
	res = fprintf(fp, "%s%s\n", hdr, ctime(&timeval));
	if (res <= 0)
		return (res);

	for (i = 0; i < NDEF; i++) {
		res = 0;

		if (tab[i].off == skip)
			continue;

		switch (tab[i].type) {
		case INT:
			res = fprintf(fp, "%s%d\n", tab[i].name,
			    FIELD(defs, &tab[i], int));
			break;
		case STR:
			res = fprintf(fp, "%s%s\n", tab[i].name,
			    FIELD(defs, &tab[i], char *));
			break;
		case PROJID:
			res = fprintf(fp, "%s%d\n", tab[i].name,
			    (int)FIELD(defs, &tab[i], projid_t));
			break;
		}

		if (res <= 0) {
			return (res);
		}
	}

	return (1);
}

/*
 * Import a default key for ordinary useradd.
 * Caller already did: ud = getusrdef();
 */
char *
userdef_get_by_uakey(struct userdefs *ud, const char *key)
{
	int i;

	for (i = 0; i < NDEF; i++) {
		if (tab[i].uakey != NULL &&
		    tab[i].type == STR &&
		    strcmp(tab[i].uakey, key) == 0)
			return (FIELD(ud, &tab[i], char *));
	}
	return (NULL);
}

/* Export a command line key to defaults for useradd -D */
void
userdef_set_by_uakey(struct userdefs *ud, const char *key, char *val)
{
	int i;

	for (i = 0; i < NDEF; i++) {
		if (tab[i].uakey != NULL &&
		    tab[i].type == STR &&
		    strcmp(tab[i].uakey, key) == 0) {
			/*
			 * Don't strlcpy here because the calling program
			 * may have changed the struct member to point to
			 * something other than our static buffers.
			 * If this leaks, it's the caller's fault.
			 */
			FIELD(ud, &tab[i], char *) = val;
		}
	}
}
