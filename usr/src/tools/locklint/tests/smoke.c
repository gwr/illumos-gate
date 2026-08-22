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

struct smoke_inner {
	int first;
	int value;
};

struct smoke {
	int head;
	struct smoke_inner nested;
	int values[3];
	struct smoke_inner *next;
};

static struct smoke global_smoke;
static int static_value;

static int
smoke_read(struct smoke *smoke)
{
	return (smoke->nested.value);
}

static void
smoke_write(struct smoke *smoke, int value)
{
	smoke->nested.value = value;
}

static int
smoke_accesses(struct smoke *arg, int index)
{
	struct smoke local;
	int local_value;

	global_smoke.head = arg->nested.value;
	local.values[index] = arg->values[1];
	arg->next->value++;
	local_value = local.values[2];
	static_value = local_value;

	return (global_smoke.head + static_value);
}
