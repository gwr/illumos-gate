/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Exercise C's adjustment of a top-level array function parameter to pointer
 * type while preserving real nested array and pointer distinctions.
 */

struct array_parameter_ops {
	int (*release)(void *, unsigned int, unsigned long[]);
#ifdef	NESTED_PARAMETER_POINTER
	int (*nested)(unsigned long **);
#else
	int (*nested)(unsigned long (*)[2]);
#endif
};

struct array_parameter_owner {
	struct array_parameter_ops *ops;
	void *handle;
};
