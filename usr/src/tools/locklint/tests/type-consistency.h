/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

struct shared_type {
#ifdef TYPE_DIFFERENT_LAYOUT
	long value;
#else
	int value;
#endif
	struct shared_type *next;
};
