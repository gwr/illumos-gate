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
 * Copyright 2015 Joyent, Inc.
 * Copyright 2024 Oxide Computer Company
 */

#define _XOPEN_SOURCE 700

#include <stdio.h>

int
main(int argc, char **argv)
{
	int x;
	x = dprintf(1, "dprintf: ");
	if (x != 9) {
		printf("FAIL (x=%d)\n", x);
		return (1);
	}
	printf("PASS\n", x);
	return (0);
}
