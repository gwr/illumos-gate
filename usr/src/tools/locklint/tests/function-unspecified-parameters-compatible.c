/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Equality comparisons do not call through either operand.  Permit a
 * function pointer with unspecified parameters to serve as a sentinel for
 * prototyped callbacks with the same return type.
 */

typedef char *caddr_t;

#define	NULL_FUNC	((int (*)())0)
#define	SLEEP_FUNC	((int (*)())1)

int
compare_pointer_callback(int (*callback)(caddr_t))
{
	return (callback == NULL_FUNC || callback == SLEEP_FUNC);
}

int
compare_other_callback(int (*callback)(int, double))
{
	return (callback == NULL_FUNC);
}
