/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify asserted lock requirements through wrappers and recursive calls.
 */

#define	ASSERT(expr)
#define	MUTEX_HELD(lock)	mutex_owned(lock)

typedef struct mutex {
	int opaque;
} mutex_t;

struct wrapper_requirement_state {
	mutex_t lock;
};

static mutex_t absolute_lock;

extern int mutex_owned(mutex_t *);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
require_held(struct wrapper_requirement_state *state)
{
	ASSERT(MUTEX_HELD(&state->lock));
}

static void
require_held_wrapper(struct wrapper_requirement_state *state)
{
	require_held(state);
}

static void
require_held_nested_wrapper(struct wrapper_requirement_state *state)
{
	require_held_wrapper(state);
}

static void
establish_held_wrapper(struct wrapper_requirement_state *state)
{
	mutex_enter(&state->lock);
	require_held(state);
	mutex_exit(&state->lock);
}

static void
conditional_held_wrapper(struct wrapper_requirement_state *state, int acquire)
{
	if (acquire) {
		mutex_enter(&state->lock);
		require_held(state);
		mutex_exit(&state->lock);
	} else {
		require_held(state);
	}
}

static void
recursive_held_wrapper(struct wrapper_requirement_state *state, int depth)
{
	if (depth == 0)
		require_held(state);
	else
		recursive_held_wrapper(state, depth - 1);
}

static void
require_absolute_held(void)
{
	ASSERT(MUTEX_HELD(&absolute_lock));
}

static void
require_absolute_wrapper(void)
{
	require_absolute_held();
}

static void
call_wrapper_unlocked(struct wrapper_requirement_state *state)
{
	require_held_wrapper(state);
}

static void
call_wrapper_locked(struct wrapper_requirement_state *state)
{
	mutex_enter(&state->lock);
	require_held_wrapper(state);
	mutex_exit(&state->lock);
}

static void
call_nested_wrapper_unlocked(struct wrapper_requirement_state *state)
{
	require_held_nested_wrapper(state);
}

static void
call_establishing_wrapper(struct wrapper_requirement_state *state)
{
	establish_held_wrapper(state);
}

static void
call_conditional_wrapper_unlocked(struct wrapper_requirement_state *state)
{
	conditional_held_wrapper(state, 0);
}

static void
call_recursive_wrapper_unlocked(struct wrapper_requirement_state *state)
{
	recursive_held_wrapper(state, 2);
}

static void
call_recursive_wrapper_locked(struct wrapper_requirement_state *state)
{
	mutex_enter(&state->lock);
	recursive_held_wrapper(state, 2);
	mutex_exit(&state->lock);
}

static void
call_absolute_wrapper_unlocked(void)
{
	require_absolute_wrapper();
}

static void
call_absolute_wrapper_locked(void)
{
	mutex_enter(&absolute_lock);
	require_absolute_wrapper();
	mutex_exit(&absolute_lock);
}
