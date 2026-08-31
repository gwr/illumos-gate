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
 * Verify that annotations introduced by a forced include apply to both its
 * external object and its translation-unit-local object.  The two unlocked
 * reads prove that each declaration retains the identity established while
 * processing the forced header.
 *
 * This test also uses forced-include.h and forced-include-second.c.
 */

struct forced_state forced_object;

static int
read_forced_object(void)
{
	return (forced_object.value);
}

static int
read_forced_static_object(void)
{
	return (forced_static_object.value);
}
