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
 * Verify that an external call is not resolved arbitrarily when multiple
 * translation units define its target.  The block-scope declaration refers
 * to either external definition, so the call graph must retain an ambiguous
 * call rather than choosing one definition.
 *
 * This test also uses ambiguous-call-first.c and ambiguous-call-second.c.
 */

static int
call_ambiguous_target(int value)
{
	int ambiguous_target;

	(void) ambiguous_target;
	{
		extern int ambiguous_target(int);

		return (ambiguous_target(value));
	}
}
