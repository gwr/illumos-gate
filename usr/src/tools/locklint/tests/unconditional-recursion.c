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
 * Characterize whether analysis continues after a call cycle with no
 * syntactically nonrecursive path.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

struct unconditional_recursion_state {
	mutex_t lock;
	int after_self;
	int after_mutual;
};

_NOTE(MUTEX_PROTECTS_DATA(unconditional_recursion_state::lock,
    unconditional_recursion_state))

static void
self_cycle(void)
{
	self_cycle();
}

static void mutual_right(void);

static void
mutual_left(void)
{
	mutual_right();
}

static void
mutual_right(void)
{
	mutual_left();
}

void check_after_self(struct unconditional_recursion_state *);
void check_after_mutual(struct unconditional_recursion_state *);

void
check_after_self(struct unconditional_recursion_state *state)
{
	self_cycle();
	state->after_self = 1;
}

void
check_after_mutual(struct unconditional_recursion_state *state)
{
	mutual_left();
	state->after_mutual = 1;
}
