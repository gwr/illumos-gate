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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Exercise root seeding and intraprocedural context traversal through
 * branches, a loop back-edge, an ordinary call, and a return.  Resolved call
 * traversal is deliberately covered by the next context-analysis increment.
 */

static int
local_increment(int value)
{
	return (value + 1);
}

int context_counting(int);

int
context_counting(int count)
{
	int total = 0;

	while (count > 0) {
		if ((count & 1) != 0)
			total += local_increment(count);
		else
			total++;
		count--;
	}
	return (total);
}
