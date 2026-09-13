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
 * Exercise macro lookup after a preceding translation unit.  The first
 * unit's token arena must still back keys retained by Sparse's macro table.
 */

#define	MACRO_LIFETIME_SECOND(value)	((value) - 1)
#define	MACRO_LIFETIME_INNER(value)	MACRO_LIFETIME_SECOND(value)

int macro_lifetime_second(int);

int
macro_lifetime_second(int value)
{
	return (MACRO_LIFETIME_INNER(value));
}
