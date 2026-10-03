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
 * Supply one origin of a repeated operation-vector type and the function
 * selected for its callback member.
 */

struct command_equivalent_ops {
	void (*start)(void);
};

extern void command_target_stop(void) __attribute__((noreturn));

static void
command_equivalent_target(void)
{
}

static void
command_equivalent_call_a(struct command_equivalent_ops *ops)
{
	ops->start();
	command_target_stop();
}
