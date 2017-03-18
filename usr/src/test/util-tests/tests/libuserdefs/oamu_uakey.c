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
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 RackTop Systems.
 * Copyright 2017 Gordon W. Ross
 */

/*
 * OAM User Test program: get/set by UA key
 * See: $SRC/cmd/oamuser/user/useradd.c
 * and: $SRC/cmd/oamuser/user/funcs.c
 * (some code copied from funcs.c)
 */

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <getopt.h>
#include <userdefs.h>
#include <user_attr.h>

void import_def(struct userdefs *ud);
void update_def(struct userdefs *ud);
void change_key(const char *, char *);

extern const char *__progname;
int vflag = 0;

int
main(int argc, char **argv)
{
	struct userdefs *ud = NULL;
	int c;

	ud = _get_userdefs();

	while ((c = getopt(argc, argv, "vK:")) != EOF) {
		switch (c) {
		case 'K': /* Keyword=value */
			change_key(NULL, optarg);
			break;
		case 'v': /* verbose */
			vflag++;
			break;
		case '?':
			(void) fprintf(stderr,
			    "usage: %s [-v] [-K key=value] [ -K ... ]\n",
			    __progname);
			break;
		}
	}

	/*
	 * Override ud values we can't set by uakey, so they
	 * don't depend on the test machine defaults file.
	 */
	ud->defrid = 123;
	ud->defgroup = 456;
	ud->defgname = "TestGroup";
	ud->defparent = "TestDefParent";
	ud->defskel = "TestDefSkel";
	ud->defshell = "TestDefShell";
	ud->definact = 3;
	ud->defexpire = "TestDefExpire";

	ud->defproj = 7;
	ud->defprojname = "TestProject";

	import_def(ud);

	if (vflag) {
		(void) printf("# Defaults:\n");
		(void) fwrite_userdefs(stdout, ud);
		(void) printf("\n");
	}

	update_def(ud);

	(void) fwrite_userdefs(stdout, ud);

	return (0);
}

/*
 * Some code copied from $SRC/cmd/oamuser/user/funcs.c
 * so we can simulate part of what that does without
 * dragging in all the parameter validation stuff.
 */

typedef struct ua_key {
	const char	*key;
	char		*newvalue;
} ua_key_t;

static ua_key_t keys[] = {
	/* Only the keywords that appear in libuserdefs */
	{ USERATTR_AUTHS_KW },
	{ USERATTR_PROFILES_KW },
	{ USERATTR_ROLES_KW },
	{ USERATTR_LIMPRIV_KW },
	{ USERATTR_DFLTPRIV_KW },
	{ USERATTR_LOCK_AFTER_RETRIES_KW },
};

#define	NKEYS	(sizeof (keys)/sizeof (ua_key_t))

/* Import default keys for ordinary useradd */
void
import_def(struct userdefs *ud)
{
	int i;

	/* Don't import the user type (skip i = 0) */
	for (i = 1; i < NKEYS; i++) {
		if (keys[i].newvalue == NULL)
			keys[i].newvalue =
			    userdef_get_by_uakey(ud, keys[i].key);
	}
}

/* Export command line keys to defaults for useradd -D */
void
update_def(struct userdefs *ud)
{
	int i;

	for (i = 0; i < NKEYS; i++) {
		if (keys[i].newvalue != NULL)
			userdef_set_by_uakey(ud, keys[i].key,
			    keys[i].newvalue);
	}
}

/*
 * Change a key, there are three different call sequences:
 *
 *		key, value	- key with option letter, value.
 *		NULL, value	- -K key=value option.
 */

void
change_key(const char *key, char *value)
{
	int i;

	if (key == NULL) {
		key = value;
		value = strchr(value, '=');
		/* Bad value */
		if (value == NULL) {
			(void) fprintf(stderr, "Invalid value (missing)\n");
			exit(EX_BADARG);
		}
		*value++ = '\0';
	}

	for (i = 0; i < NKEYS; i++) {
		if (strcmp(key, keys[i].key) == 0) {
			if (keys[i].newvalue != NULL) {
				/* Can't set a value twice */
				(void) fprintf(stderr,
				    "Invalid value (duplicate)\n");
				exit(EX_BADARG);
			}

			keys[i].newvalue = value;
			return;
		}
	}
	(void) fprintf(stderr, "Invalid key: %s\n", key);
	exit(EX_BADARG);
}
