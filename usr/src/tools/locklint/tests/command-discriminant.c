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
 * Verify constant-only formal discriminants, including forwarding and an
 * unknown-value control which must retain both branches.
 */

typedef struct rwlock {
	int opaque;
} rwlock_t;

enum discriminant_mode {
	DISCRIMINANT_INNER = 1,
	DISCRIMINANT_OUTER = 2
};

rwlock_t discriminant_inner;
rwlock_t discriminant_outer;

int discriminant_inner_data;
int discriminant_outer_data;
int discriminant_forward_inner_data;
int discriminant_forward_outer_data;
int discriminant_unknown_data;

extern void rw_wrlock(rwlock_t *);
extern void rw_exit(rwlock_t *);

static void
discriminant_inner_callback(void)
{
	discriminant_inner_data++;
}

static void
discriminant_outer_callback(void)
{
	discriminant_outer_data++;
}

static void
discriminant_forward_inner_callback(void)
{
	discriminant_forward_inner_data++;
}

static void
discriminant_forward_outer_callback(void)
{
	discriminant_forward_outer_data++;
}

static void
discriminant_unknown_callback(void)
{
	discriminant_unknown_data++;
}

static void
discriminant_neutral_callback(void)
{
}

void
discriminant_dispatch(void (*func)(void), int mode)
{
	if (mode == 1) {
		rw_wrlock(&discriminant_inner);
		(*func)();
		rw_exit(&discriminant_inner);
	} else {
		rw_wrlock(&discriminant_outer);
		(*func)();
		rw_exit(&discriminant_outer);
	}
}

void
discriminant_dispatch_ne(void (*func)(void), int mode)
{
	if (mode != DISCRIMINANT_OUTER) {
		rw_wrlock(&discriminant_inner);
		(*func)();
		rw_exit(&discriminant_inner);
	} else {
		rw_wrlock(&discriminant_outer);
		(*func)();
		rw_exit(&discriminant_outer);
	}
}

void
discriminant_enum_formal(enum discriminant_mode mode)
{
	(void) mode;
}

void
discriminant_forward(void (*func)(void), int mode)
{
	discriminant_dispatch(func, mode);
}

void
discriminant_call_inner(void)
{
	discriminant_dispatch(discriminant_inner_callback, 1);
}

void
discriminant_call_outer(void)
{
	discriminant_dispatch(discriminant_outer_callback, 2);
}

void
discriminant_call_ne_inner(void)
{
	discriminant_dispatch_ne(discriminant_inner_callback,
	    DISCRIMINANT_INNER);
}

void
discriminant_call_ne_outer(void)
{
	discriminant_dispatch_ne(discriminant_outer_callback,
	    DISCRIMINANT_OUTER);
}

void
discriminant_call_forward_inner(void)
{
	discriminant_forward(discriminant_forward_inner_callback, 1);
}

void
discriminant_call_forward_outer(void)
{
	discriminant_forward(discriminant_forward_outer_callback, 2);
}

void
discriminant_call_same_inner(void)
{
	discriminant_dispatch(discriminant_neutral_callback, 1);
}

void
discriminant_call_same_outer(void)
{
	discriminant_dispatch(discriminant_neutral_callback, 2);
}

void
discriminant_call_unknown(int mode)
{
	discriminant_dispatch(discriminant_unknown_callback, mode);
}
