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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Exercise context reuse across multiple callers, direct recursion, and
 * mutual recursion.  Empty semantic state makes each function converge on
 * one context while preserving every distinct incoming call-site edge.
 */

static int
leaf(int value)
{
	return (value + 1);
}

static int
binding_leaf(int *first, int *second)
{
	return (*first + *second);
}

static int
binding_wrapper(int *first, int *second)
{
	return (binding_leaf(first + 1, second + 1));
}

static int
direct_recursive(int value)
{
	if (value == 0)
		return (leaf(value));
	return (direct_recursive(value - 1));
}

static int mutual_b(int);

static int
mutual_a(int value)
{
	if (value == 0)
		return (value);
	return (mutual_b(value - 1));
}

static int
mutual_b(int value)
{
	if (value == 0)
		return (value);
	return (mutual_a(value - 1));
}

int context_calls(int, int *, int *);

int
context_calls(int value, int *first, int *second)
{
	return (direct_recursive(value) + mutual_a(value) + leaf(value) +
	    binding_leaf(first + 1, first + 1) +
	    binding_leaf(first + 1, first + 1) +
	    binding_leaf(first + 1, second + 1) +
	    binding_wrapper(first, first) + binding_wrapper(first, second));
}
