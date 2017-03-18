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
 * Copyright 2016 Gordon W. Ross
 */

/*
 * OAM User Test program
 */

#include	<sys/types.h>
#include	<stdio.h>
#include	<string.h>
#include	<userdefs.h>
#include	<stdlib.h>
#include	<stddef.h>
#include	<getopt.h>

extern const char *__progname;
boolean_t rflag = B_FALSE;

int
main(int argc, char **argv)
{
	struct userdefs *ud;
	int c, i, errs = 0;

	while ((c = getopt(argc, argv, "r")) != EOF) {
		switch (c) {
		case 'r': /* role */
			rflag = B_TRUE;
			break;
		case '?':
			(void) fprintf(stderr, "usage: %s [-r] [file [file...]]\n",
				__progname);
			break;
		}
	}

	(void) printf("# Defaults:\n");
	if (rflag) {
		ud = _get_roledefs();
		fwrite_roledefs(stdout, ud);
	} else {
		ud = _get_userdefs();
		fwrite_userdefs(stdout, ud);
	}

	if (optind == argc)
		return (0);

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

	(void) printf("# Final:\n");
	if (rflag) {
		fwrite_roledefs(stdout, ud);
	} else {
		fwrite_userdefs(stdout, ud);
	}

	return ((errs == 0) ? 0 : 1);
}

