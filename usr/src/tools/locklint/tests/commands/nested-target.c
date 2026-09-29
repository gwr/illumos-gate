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
 * Verify that indirect-call guidance names the aggregate which directly owns
 * the function-pointer member, not an outer aggregate in the access path.
 */

typedef void (*nested_target_t)(void);

struct nested_target_ops {
	nested_target_t finish;
};

struct nested_target_outer {
	struct nested_target_ops *ops;
};

extern void nested_target_stop(void) __attribute__((noreturn));

static void
nested_target_call(struct nested_target_outer *outer)
{
	outer->ops->finish();
	nested_target_stop();
}
