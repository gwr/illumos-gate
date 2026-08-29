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

struct callback_ops {
	int (*open)(int);
};

static int
driver_open(int value)
{
	return (value);
}

static int
explicit_target(int value)
{
	return (value + 1);
}

static int
indirect_target(int value)
{
	return (value + 2);
}

static int
direct_only(int value)
{
	return (value + 3);
}

static struct callback_ops driver_ops = {
	.open = driver_open
};

static int (*explicit_handler)(int) = &explicit_target;
static int (*current_handler)(int);

static void
copy_handler(void)
{
	current_handler = explicit_handler;
}

static void
install_handler(int (*handler)(int))
{
	current_handler = handler;
}

static int
call_indirect(int value)
{
	return (current_handler(value));
}

static int
call_block_static(int value)
{
	static int (*handler)(int) = indirect_target;

	return (handler(value));
}

static int
exercise_pointers(int value)
{
	copy_handler();
	install_handler(indirect_target);
	return (direct_only(value) + call_indirect(value) +
	    call_block_static(value) + driver_ops.open(value));
}
