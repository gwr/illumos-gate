/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify that an old-style function pointer with an unspecified parameter
 * list accepts calls with arguments, as required by C.
 */

#ifdef VOID_PROTOTYPE

static int
unspecified_target(void)
{
	return (0);
}

static int
unspecified_invoke(int (*callback)(void))
{
	return (callback(1, 2));
}

#else

static int
unspecified_target(int first, int second)
{
	return (first + second);
}

static int
unspecified_invoke(int (*callback)())
{
	return (callback(1, 2));
}

#endif

int
unspecified_root(void)
{
	return (unspecified_invoke(unspecified_target));
}
