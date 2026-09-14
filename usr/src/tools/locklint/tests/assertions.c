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
 * Verify that recognized ASSERT and VERIFY predicates refine lock state
 * without analyzing the macro implementation.  Protected data in an
 * assertion remains visible even when its configured macro is empty.
 * Equivalent held and not-held spellings distinguish predicate polarity, an
 * active macro body proves that helper calls such as assfail() remain hidden,
 * and competition assertions show the analogous state transition for
 * unprotected access.
 */

#ifdef __lock_lint
#include <sys/debug.h>
#else
#define	_NOTE(arg)
#define	ASSERT(expr)
#define	VERIFY(expr)	((void)(expr))
#endif
#define	MUTEX_HELD(lock)	mutex_owned(lock)
#define	MUTEX_NOT_HELD(lock)	(!mutex_owned(lock))

typedef struct mutex {
	int opaque;
} mutex_t;

struct assertion_state {
	int value;
	mutex_t lock;
};

_NOTE(MUTEX_PROTECTS_DATA(assertion_state::lock,
    assertion_state::value))

extern int mutex_owned(mutex_t *);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
#ifndef __lock_lint
extern int assfail(void);
#endif
extern int assertion_unprotected(struct assertion_state *);
extern void assertion_expression_access(struct assertion_state *);
extern int assertion_macro_held(struct assertion_state *);
extern int assertion_direct_held(struct assertion_state *);
extern int assertion_macro_not_held(struct assertion_state *);
extern int assertion_negated(struct assertion_state *);
extern int assertion_zero_comparison(struct assertion_state *);
extern int assertion_active_held(struct assertion_state *);

int
assertion_unprotected(struct assertion_state *state)
{
	return (state->value);
}

void
assertion_expression_access(struct assertion_state *state)
{
	ASSERT((state->value & 1) == 0);
}

int
assertion_macro_held(struct assertion_state *state)
{
	ASSERT(MUTEX_HELD(&state->lock));
	return (state->value);
}

int
assertion_direct_held(struct assertion_state *state)
{
	VERIFY(mutex_owned(&state->lock));
	return (state->value);
}

int
assertion_macro_not_held(struct assertion_state *state)
{
	mutex_enter(&state->lock);
	ASSERT(MUTEX_NOT_HELD(&state->lock));
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
	return (0);
}

int
assertion_negated(struct assertion_state *state)
{
	mutex_enter(&state->lock);
	ASSERT(!mutex_owned(&state->lock));
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
	return (0);
}

int
assertion_zero_comparison(struct assertion_state *state)
{
	mutex_enter(&state->lock);
	ASSERT(mutex_owned(&state->lock) == 0);
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
	return (0);
}

#ifndef __lock_lint
#undef	ASSERT
#define	ASSERT(expr)	((void)((expr) || assfail()))
#endif

int
assertion_active_held(struct assertion_state *state)
{
	ASSERT(MUTEX_HELD(&state->lock));
	return (state->value);
}

#define	NO_COMPETING_THREADS	1

static int
assertion_no_competing_threads(struct assertion_state *state)
{
	ASSERT(NO_COMPETING_THREADS);
	return (state->value);
}

static int
assertion_resets_competition(struct assertion_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	ASSERT(NO_COMPETING_THREADS);
	return (state->value);
}

static int
note_no_competing_threads(struct assertion_state *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS)
	return (state->value);
}

static void
assertion_parenthesized_negated(struct assertion_state *state)
{
	mutex_enter(&state->lock);
	ASSERT(!(mutex_owned(&state->lock)));
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
}
