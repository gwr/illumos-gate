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
 * Copyright 2017 Gordon W. Ross
 */

/*
 * OAM User Test program: read/write defaults
 */

#include	<sys/types.h>
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<stddef.h>
#include	<getopt.h>
#include	<userdefs.h>

extern const char *__progname;
boolean_t rflag = B_FALSE;
int vflag = 0;

int
main(int argc, char **argv)
{
	struct userdefs *ud;
	int c, i, errs = 0;

	while ((c = getopt(argc, argv, "rv")) != EOF) {
		switch (c) {
		case 'r': /* role */
			rflag = B_TRUE;
			break;
		case 'v': /* verbose */
			vflag++;
			break;
		case '?':
			(void) fprintf(stderr,
			    "usage: %s [-rv] [file [file...]]\n",
			    __progname);
			break;
		}
	}

	if (rflag) {
		ud = _get_roledefs();
	} else {
		ud = _get_userdefs();
	}

	if (vflag) {
		(void) printf("# Defaults:\n");
		if (rflag) {
			(void) fwrite_roledefs(stdout, ud);
		} else {
			(void) fwrite_userdefs(stdout, ud);
		}
		(void) printf("\n");
	}

	for (i = optind; i < argc; i++) {
		FILE *fp;

		fp = fopen(argv[i], "r");
		if (fp == NULL) {
			perror(argv[i]);
			errs++;
		} else {
			fread_defs(fp, ud, rflag);
			(void) fclose(fp);
		}
	}

	if (rflag) {
		(void) fwrite_roledefs(stdout, ud);
	} else {
		(void) fwrite_userdefs(stdout, ud);
	}

	return ((errs == 0) ? 0 : 1);
}
