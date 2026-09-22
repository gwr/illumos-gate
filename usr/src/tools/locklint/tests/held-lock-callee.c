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
 * Characterize calls to a balanced callee while the caller already holds the
 * same lock.  Locklint reports the invalid nested acquisition and terminates
 * only that semantic path, through both direct and formal-callback calls.
 * Equivalent local pointer aliases identify the same lock.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

struct held_lock_state {
	mutex_t lock;
};

struct held_lock_alias {
	struct held_lock_state *state;
};

typedef void (*lock_callback_t)(struct held_lock_state *);
typedef void (*alias_callback_t)(struct held_lock_alias *);

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

#ifndef HELD_LOCK_CALLEE_VARIANT
#define	HELD_LOCK_CALLEE_VARIANT	0
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 1 || \
    HELD_LOCK_CALLEE_VARIANT == 2
static void
balanced_callee(struct held_lock_state *state)
{
	mutex_enter(&state->lock);
	mutex_exit(&state->lock);
}
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 2
static void
invoke_callback(struct held_lock_state *state, lock_callback_t callback)
{
	callback(state);
}
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 1
static void
call_direct_while_held(struct held_lock_state *state)
{
	mutex_enter(&state->lock);
	balanced_callee(state);
	mutex_exit(&state->lock);
}
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 2
static void
call_callback_while_held(struct held_lock_state *state)
{
	mutex_enter(&state->lock);
	invoke_callback(state, balanced_callee);
	mutex_exit(&state->lock);
}
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 3 || \
    HELD_LOCK_CALLEE_VARIANT == 4
static void
balanced_alias_callee(struct held_lock_alias *alias)
{
	struct held_lock_state *state = alias->state;

	mutex_enter(&alias->state->lock);
	mutex_exit(&state->lock);
}
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 4
static void
invoke_alias_callback(struct held_lock_alias *alias,
    alias_callback_t callback)
{
	callback(alias);
}
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 3
static void
call_alias_direct_while_held(struct held_lock_alias *alias)
{
	mutex_enter(&alias->state->lock);
	balanced_alias_callee(alias);
	mutex_exit(&alias->state->lock);
}
#endif

#if HELD_LOCK_CALLEE_VARIANT == 0 || HELD_LOCK_CALLEE_VARIANT == 4
static void
call_alias_callback_while_held(struct held_lock_alias *alias)
{
	mutex_enter(&alias->state->lock);
	invoke_alias_callback(alias, balanced_alias_callee);
	mutex_exit(&alias->state->lock);
}
#endif
