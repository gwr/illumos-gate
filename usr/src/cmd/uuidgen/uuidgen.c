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
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * uuidgen(1)
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <libgen.h>
#include <stdlib.h>
#include <errno.h>
#include <uuid/uuid.h>

#ifndef	TEXT_DOMAIN		/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it wasn't */
#endif

static char *progname;
static int r_flag, t_flag;
static char buf[40];

static void
usage(void)
{
	(void) fprintf(stderr, gettext("usage: %s [-r] [-t]\n"), progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	uuid_t	uuid;
	int	c;

	(void) setlocale(LC_ALL, "");
	(void) textdomain(TEXT_DOMAIN);

	if ((progname = strrchr(argv[0], '/')) != NULL)
		progname++;
	else
		progname = argv[0];

	while ((c = getopt(argc, argv, "rt?")) != -1) {
		switch (c) {
		case 'r':
			r_flag++;
			break;
		case 't':
			t_flag++;
			break;
		case '?':
			usage();
		}
	}

	if (r_flag) {
		uuid_generate_random(uuid);
	} else if (t_flag) {
		uuid_generate_time(uuid);
	} else {
		uuid_generate(uuid);
	}

	uuid_unparse(uuid, buf);
	printf("%s\n", buf);

	return (0);
}
