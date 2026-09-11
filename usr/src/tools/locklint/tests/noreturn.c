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
 * Verify compiler-visible noreturn declarations terminate locklint analysis
 * without requiring a source annotation.
 */

#define	_NOTE(arg)

typedef struct kmutex {
	int opaque;
} kmutex_t;

struct protected_state {
	kmutex_t lock;
	int value;
};

_NOTE(MUTEX_PROTECTS_DATA(protected_state::lock, protected_state::value))

extern _Noreturn void c11_noreturn(void);
extern void gnu_noreturn(void) __attribute__((noreturn));

static void
access_after_c11_noreturn(struct protected_state *state)
{
	c11_noreturn();
	state->value = 1;
}

static void
access_after_gnu_noreturn(struct protected_state *state)
{
	gnu_noreturn();
	state->value = 1;
}
