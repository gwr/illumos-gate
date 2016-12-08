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
 */

/*LINTLIBRARY*/

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
#include	"userdisp.h"
#include	"funcs.h"
#include	"messages.h"

/* Print out a NL when the line gets too long */
#define	PRINTNL()	\
	if (outcount > 40) { \
		outcount = 0; \
		(void) fprintf(fptr, "\n"); \
	}

/*
 * getusrdef - get the user defaults file for the type of
 * user entry (user or role).  See libuserdefs
 */

struct userdefs *
getusrdef(char *usertype)
{
	struct userdefs *ud;

	if (is_role(usertype))
		ud = _get_roledefs();
	else
		ud = _get_userdefs();

	return (ud);
}

void
dispusrdef(FILE *fptr, unsigned flags, char *usertype)
{
	struct userdefs *deflts = getusrdef(usertype);
	int outcount = 0;

	/* Print out values */

	if (flags & D_GROUP) {
		outcount += fprintf(fptr, "group=%s,%ld  ",
		    deflts->defgname, deflts->defgroup);
		PRINTNL();
	}

	if (flags & D_PROJ) {
		outcount += fprintf(fptr, "project=%s,%ld  ",
		    deflts->defprojname, deflts->defproj);
		PRINTNL();
	}

	if (flags & D_BASEDIR) {
		outcount += fprintf(fptr, "basedir=%s  ", deflts->defparent);
		PRINTNL();
	}

	if (flags & D_RID) {
		outcount += fprintf(fptr, "rid=%ld  ", deflts->defrid);
		PRINTNL();
	}

	if (flags & D_SKEL) {
		outcount += fprintf(fptr, "skel=%s  ", deflts->defskel);
		PRINTNL();
	}

	if (flags & D_SHELL) {
		outcount += fprintf(fptr, "shell=%s  ", deflts->defshell);
		PRINTNL();
	}

	if (flags & D_INACT) {
		outcount += fprintf(fptr, "inactive=%d  ", deflts->definact);
		PRINTNL();
	}

	if (flags & D_EXPIRE) {
		outcount += fprintf(fptr, "expire=%s  ", deflts->defexpire);
		PRINTNL();
	}

	if (flags & D_AUTH) {
		outcount += fprintf(fptr, "auths=%s  ", deflts->defauth);
		PRINTNL();
	}

	if (flags & D_PROF) {
		outcount += fprintf(fptr, "profiles=%s  ", deflts->defprof);
		PRINTNL();
	}

	if ((flags & D_ROLE) &&
	    (!is_role(usertype))) {
		outcount += fprintf(fptr, "roles=%s  ", deflts->defrole);
		PRINTNL();
	}

	if (flags & D_LPRIV) {
		outcount += fprintf(fptr, "limitpriv=%s  ",
		    deflts->deflimpriv);
		PRINTNL();
	}

	if (flags & D_DPRIV) {
		outcount += fprintf(fptr, "defaultpriv=%s  ",
		    deflts->defdfltpriv);
		PRINTNL();
	}

	if (flags & D_LOCK) {
		outcount += fprintf(fptr, "lock_after_retries=%s  ",
		    deflts->deflock_after_retries);
	}

	if (outcount > 0)
		(void) fprintf(fptr, "\n");
}

/*
 * putusrdef -
 * 	changes default values in defadduser file
 */
int
putusrdef(struct userdefs *defs, char *usertype)
{
	FILE *fp = NULL;	/* default file - fptr */
	boolean_t locked = B_FALSE;
	int res;
	int ex = EX_UPDATE;

	if (is_role(usertype)) {
		fp = fopen(DEFROLEFILE, "w");
	} else {
		fp = fopen(DEFFILE, "w");
	}
	if (fp == NULL) {
		errmsg(M_FAILED);
		goto out;
	}

	if (lockf(fileno(fp), F_LOCK, 0) != 0) {
		/* print error if can't lock whole of DEFFILE */
		errmsg(M_UPDATE, "created");
		goto out;
	}
	locked = B_TRUE;

	if (is_role(usertype)) {
		res = fwrite_roledefs(fp, defs);
	} else {
		res = fwrite_userdefs(fp, defs);
	}
	if (res <= 0) {
		errmsg(M_UPDATE, "created");
		goto out;
	}
	ex = EX_SUCCESS;

out:
	if (fp != NULL) {
		if (locked)
			(void) lockf(fileno(fp), F_ULOCK, 0);
		(void) fclose(fp);
	}

	return (ex);
}
