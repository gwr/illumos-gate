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
 * Exercise a callback reached through an operation-vector member whose
 * target is supplied by command policy.
 */

struct static_entry_ops {
	void (*run)(void);
};

extern struct static_entry_ops static_entry_ops_instance;

void
static_entry_invoke(void)
{
	static_entry_ops_instance.run();
}
