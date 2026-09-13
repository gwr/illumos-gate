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
 * Exercise macro expansion in the first of two translation units.  Locklint
 * must retain this unit's preprocessing tokens until the second unit has
 * finished using Sparse's process-wide macro table.
 */

#define	MACRO_LIFETIME_FIRST(value)	((value) + 1)
#define	MACRO_LIFETIME_OUTER(value)	MACRO_LIFETIME_FIRST(value)

int macro_lifetime_first(int);

int
macro_lifetime_first(int value)
{
	return (MACRO_LIFETIME_OUTER(value));
}
