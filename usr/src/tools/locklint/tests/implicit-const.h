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
 * Declare const objects in a separate translation unit so characterization
 * accesses cannot be replaced by their initializer values.
 */

#ifndef IMPLICIT_CONST_H
#define	IMPLICIT_CONST_H

struct const_group {
	int first;
	int second;
};

extern const int implicit_const_value;
extern const struct const_group implicit_const_record;

#endif /* IMPLICIT_CONST_H */
