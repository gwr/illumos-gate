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
 * Characterize whether ASSERT(NO_COMPETING_THREADS) supplies authoritative
 * state after its program point and how far that state propagates.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/note.h>
#else
#define	_NOTE(arg)
#define	ASSERT(expr)

typedef struct kmutex {
	int opaque;
} kmutex_t;
#endif

#define	NO_COMPETING_THREADS	1

struct assertion_competition_state {
	kmutex_t lock;
	int value;
};

_NOTE(MUTEX_PROTECTS_DATA(assertion_competition_state::lock,
    assertion_competition_state::value))

static void
assertion_competition_helper(struct assertion_competition_state *state)
{
	state->value = 1;
}

static void
assertion_competition_immediate(struct assertion_competition_state *state)
{
	ASSERT(NO_COMPETING_THREADS);
	state->value = 1;
}

static void
assertion_competition_later(struct assertion_competition_state *state,
    int branch)
{
	ASSERT(NO_COMPETING_THREADS);
	if (branch)
		branch = 0;
	state->value = 1;
}

static void
assertion_competition_one_branch(struct assertion_competition_state *state,
    int refine)
{
	if (refine)
		ASSERT(NO_COMPETING_THREADS);
	state->value = 1;
}

static void
assertion_competition_callee(struct assertion_competition_state *state)
{
	ASSERT(NO_COMPETING_THREADS);
	assertion_competition_helper(state);
}

static void
assertion_competition_reentered(struct assertion_competition_state *state)
{
	ASSERT(NO_COMPETING_THREADS);
	_NOTE(COMPETING_THREADS_NOW)
	state->value = 1;
	_NOTE(NO_COMPETING_THREADS_NOW)
}

static void
assertion_competition_invalid(struct assertion_competition_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	ASSERT(NO_COMPETING_THREADS);
	state->value = 1;
	_NOTE(NO_COMPETING_THREADS_NOW)
}
