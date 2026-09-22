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
 * Characterize protected reads and modifications for source-level
 * read-modify-write expressions.  Keep the source forms in separate functions
 * so their lowered accesses and diagnostics can be compared independently.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

struct rmw_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(rmw_state::lock, rmw_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

void rmw_add(struct rmw_state *, int);
void rmw_postincrement(struct rmw_state *);
void rmw_explicit(struct rmw_state *, int);
void rmw_reused_condition(struct rmw_state *, int);
void rmw_plain_store(struct rmw_state *, int);
void rmw_sequence(struct rmw_state *, int);
void rmw_held(struct rmw_state *, int, int);

void
rmw_add(struct rmw_state *state, int value)
{
	state->value += value;
}

void
rmw_postincrement(struct rmw_state *state)
{
	state->value++;
}

void
rmw_explicit(struct rmw_state *state, int value)
{
	state->value = state->value + value;
}

void
rmw_reused_condition(struct rmw_state *state, int mask)
{
	if ((state->value & mask) != 0)
		state->value &= ~mask;
}

void
rmw_plain_store(struct rmw_state *state, int value)
{
	state->value = value;
}

void
rmw_sequence(struct rmw_state *state, int value)
{
	state->value += value;
	state->value++;
	state->value = state->value + value;
}

void
rmw_held(struct rmw_state *state, int value, int mask)
{
	mutex_enter(&state->lock);
	state->value += value;
	state->value++;
	state->value = state->value + value;
	if ((state->value & mask) != 0)
		state->value &= ~mask;
	mutex_exit(&state->lock);
}
