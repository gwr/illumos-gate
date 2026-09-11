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
 * Characterize declared rwlock upgrade and downgrade side effects.  Separate
 * variants keep invalid contracts from obscuring caller-state behavior.
 */

#ifdef __lock_lint
#include <sys/note.h>
#include <sys/rwlock.h>
#else
#define	_NOTE(arg)

typedef struct krwlock {
	void *opaque[1];
} krwlock_t;

typedef enum krw {
	RW_WRITER,
	RW_READER
} krw_t;
#endif

#ifndef RWLOCK_TRANSITION_EFFECT_VARIANT
#define	RWLOCK_TRANSITION_EFFECT_VARIANT	0
#endif

struct transition_effect_state {
	krwlock_t lock;
	int value;
};

_NOTE(RWLOCK_PROTECTS_DATA(transition_effect_state::lock,
    transition_effect_state::value))

#ifndef __lock_lint
extern void rw_enter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
extern void rw_downgrade(krwlock_t *);
extern int rw_tryupgrade(krwlock_t *);
#endif

#if RWLOCK_TRANSITION_EFFECT_VARIANT == 1

static void
declared_upgrade_valid(struct transition_effect_state *state)
{
	_NOTE(LOCK_UPGRADED_AS_SIDE_EFFECT(state->lock))
	if (!rw_tryupgrade(&state->lock))
		for (;;)
			;
}

static void
declared_upgrade_ignored(struct transition_effect_state *state)
{
	_NOTE(LOCK_UPGRADED_AS_SIDE_EFFECT(state->lock))
	(void) rw_tryupgrade(&state->lock);
}

static int
call_upgrade_reader(struct transition_effect_state *state)
{
	int value;

	rw_enter(&state->lock, RW_READER);
	declared_upgrade_valid(state);
	value = state->value;
	state->value = value;
	rw_exit(&state->lock);
	return (value);
}

static void
call_upgrade_writer(struct transition_effect_state *state)
{
	rw_enter(&state->lock, RW_WRITER);
	declared_upgrade_ignored(state);
	rw_exit(&state->lock);
}

static void
call_upgrade_unheld(struct transition_effect_state *state)
{
	declared_upgrade_ignored(state);
	rw_exit(&state->lock);
}

static void
declared_upgrade_missing(struct transition_effect_state *state)
{
	_NOTE(LOCK_UPGRADED_AS_SIDE_EFFECT(state->lock))
	(void) state;
}

static void
declared_upgrade_conditional(struct transition_effect_state *state,
    int upgrade)
{
	_NOTE(LOCK_UPGRADED_AS_SIDE_EFFECT(state->lock))
	if (upgrade)
		(void) rw_tryupgrade(&state->lock);
}

#elif RWLOCK_TRANSITION_EFFECT_VARIANT == 2

static void
declared_downgrade_valid(struct transition_effect_state *state)
{
	_NOTE(LOCK_DOWNGRADED_AS_SIDE_EFFECT(state->lock))
	rw_downgrade(&state->lock);
}

static int
call_downgrade_writer(struct transition_effect_state *state)
{
	int value;

	rw_enter(&state->lock, RW_WRITER);
	declared_downgrade_valid(state);
	value = state->value;
	state->value = value;
	rw_exit(&state->lock);
	return (value);
}

static void
call_downgrade_reader(struct transition_effect_state *state)
{
	rw_enter(&state->lock, RW_READER);
	declared_downgrade_valid(state);
	rw_exit(&state->lock);
}

static void
call_downgrade_unheld(struct transition_effect_state *state)
{
	declared_downgrade_valid(state);
	rw_exit(&state->lock);
}

static void
declared_downgrade_missing(struct transition_effect_state *state)
{
	_NOTE(LOCK_DOWNGRADED_AS_SIDE_EFFECT(state->lock))
	(void) state;
}

static void
declared_downgrade_conditional(struct transition_effect_state *state,
    int downgrade)
{
	_NOTE(LOCK_DOWNGRADED_AS_SIDE_EFFECT(state->lock))
	if (downgrade)
		rw_downgrade(&state->lock);
}

#endif
