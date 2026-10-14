/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * The relaxed equality-comparison rule must not hide incompatible function
 * return types.
 */

#define	NULL_FUNC	((int (*)())0)

int
compare_return_type(void (*callback)(char *))
{
	return (callback == NULL_FUNC);
}
