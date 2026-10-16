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
 * Verify that entry assertions recognize the underlying illumos readers-
 * writer lock tag whether the global object uses the typedef or the tag.
 */

typedef struct _krwlock {
	void *opaque[1];
} krwlock_t;

krwlock_t command_assert_krwlock_typedef_lock;
struct _krwlock command_assert_krwlock_tag_lock;

void
command_assert_krwlock_typedef(void)
{
}

void
command_assert_krwlock_tag(void)
{
}
