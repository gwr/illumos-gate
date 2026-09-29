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
 * Call through a layout-equal operation-vector type defined at a distinct
 * source origin from the declared target.
 */

struct command_equivalent_ops {
	void (*start)(void);
#ifdef COMMAND_EQUIVALENT_INCONSISTENT
	int other;
#endif
};

extern void command_target_stop(void) __attribute__((noreturn));

static void
command_equivalent_call(struct command_equivalent_ops *ops)
{
	ops->start();
	command_target_stop();
}
