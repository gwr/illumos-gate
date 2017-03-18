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

#include	<sys/types.h>
#include	<stdio.h>
#include	<string.h>
#define	_USERDEFS_INTERNAL 1
#include	<userdefs.h>
#include	<user_attr.h>
#include	<limits.h>
#include	<stdlib.h>
#include	<stddef.h>
#include	<time.h>
#include	<unistd.h>

#define	SKIPWS(ptr)	while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++

static char *dup_to_nl(char *);
static int fwrite_defs(FILE *, struct userdefs *, char *, ptrdiff_t);

static struct userdefs defaults = {
	DEFRID, DEFGROUP, DEFGNAME, DEFPARENT, DEFSKL,
	DEFSHL, DEFINACT, DEFEXPIRE, DEFAUTH, DEFPROF,
	DEFROLE, DEFPROJ, DEFPROJNAME, DEFLIMPRIV,
	DEFDFLTPRIV, DEFLOCK_AFTER_RETRIES
};

static struct userdefs roledefs = {
	DEFRID, DEFGROUP, DEFGNAME, DEFPARENT, DEFSKL,
	DEFROLESHL,	/* role! */
	DEFINACT, DEFEXPIRE, DEFAUTH,
	DEFROLEPROF,	/* role! */
	DEFROLE, DEFPROJ, DEFPROJNAME, DEFLIMPRIV,
	DEFDFLTPRIV, DEFLOCK_AFTER_RETRIES
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

static const parsent_t tab[] = {						/* defaults */
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
 *  in it to override the above values.
 */

struct userdefs *
_get_roledefs()
{
	FILE *fp;

	fp = fopen(DEFROLEFILE, "r");
	if (fp == NULL)
		return (&roledefs);

	fread_defs(fp, &roledefs, B_TRUE);

	(void) fclose(fp);

	return (&roledefs);
}

struct userdefs *
_get_userdefs()
{
	FILE *fp;

	fp = fopen(DEFFILE, "r");
	if (fp == NULL)
		return (&defaults);

	fread_defs(fp, &defaults, B_FALSE);

	(void) fclose(fp);

	return (&defaults);
}

void
fread_defs(FILE *fp, struct userdefs *ud, boolean_t role)
{
	char instr[512], *ptr;
	const parsent_t *pe;

	while (fgets(instr, sizeof (instr), fp) != NULL) {
		ptr = instr;

		SKIPWS(ptr);

		if (*ptr == '#')
			continue;

		pe = scan(&ptr);

		if (pe != NULL) {
			/* If a role, should not see defrole, but... */
			if (role && pe->off == DEFOFF(defrole))
				continue;

			switch (pe->type) {
			case INT:
				FIELD(&defaults, pe, int) =
					(int)strtol(ptr, NULL, 10);
				break;
			case PROJID:
				FIELD(&defaults, pe, projid_t) =
					(projid_t)strtol(ptr, NULL, 10);
				break;
			case STR:
				FIELD(&defaults, pe, char *) = dup_to_nl(ptr);
				break;
			}
		}
	}
}

static char *
dup_to_nl(char *from)
{
	char *res = strdup(from);

	char *p = strchr(res, '\n');
	if (p)
		*p = '\0';

	return (res);
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
		    strcmp(tab[i].uakey, key) == 0)
			FIELD(ud, &tab[i], char *) = val;
	}
}
