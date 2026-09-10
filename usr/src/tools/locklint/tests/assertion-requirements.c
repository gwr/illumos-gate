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
 * Verify point-sensitive asserted lock requirements at direct calls.
 */

#define	_NOTE(arg)
#define	ASSERT(expr)
#define	MUTEX_HELD(lock)	mutex_owned(lock)
#define	MUTEX_NOT_HELD(lock)	(!mutex_owned(lock))
#define	RW_READ_HELD(lock)	rw_read_held(lock)
#define	RW_WRITE_HELD(lock)	rw_write_held(lock)

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;

struct assertion_requirement_state {
	mutex_t mutex;
	rwlock_t rwlock;
};

extern int mutex_owned(mutex_t *);
extern int rw_read_held(rwlock_t *);
extern int rw_write_held(rwlock_t *);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void rw_rdlock(rwlock_t *);
extern void rw_wrlock(rwlock_t *);
extern void rw_exit(rwlock_t *);

static void
require_mutex_held(struct assertion_requirement_state *state)
{
	ASSERT(MUTEX_HELD(&state->mutex));
}

static void
require_mutex_not_held(struct assertion_requirement_state *state)
{
	ASSERT(MUTEX_NOT_HELD(&state->mutex));
}

static void
require_read_held(struct assertion_requirement_state *state)
{
	ASSERT(RW_READ_HELD(&state->rwlock));
}

static void
require_write_held(struct assertion_requirement_state *state)
{
	ASSERT(RW_WRITE_HELD(&state->rwlock));
}

static void
assert_after_acquire(struct assertion_requirement_state *state)
{
	mutex_enter(&state->mutex);
	ASSERT(MUTEX_HELD(&state->mutex));
	mutex_exit(&state->mutex);
}

static void
assert_after_release(struct assertion_requirement_state *state)
{
	mutex_exit(&state->mutex);
	ASSERT(MUTEX_NOT_HELD(&state->mutex));
}

static void
assert_held_conditionally(struct assertion_requirement_state *state, int check)
{
	if (check)
		ASSERT(MUTEX_HELD(&state->mutex));
}

static void
assert_held_unreachable(struct assertion_requirement_state *state)
{
	(void) state;
	return;
	ASSERT(MUTEX_HELD(&state->mutex));
}

static void
call_require_held_locked(struct assertion_requirement_state *state)
{
	mutex_enter(&state->mutex);
	require_mutex_held(state);
	mutex_exit(&state->mutex);
}

static void
call_require_held_unlocked(struct assertion_requirement_state *state)
{
	require_mutex_held(state);
}

static void
call_require_not_held_unlocked(struct assertion_requirement_state *state)
{
	require_mutex_not_held(state);
}

static void
call_require_not_held_locked(struct assertion_requirement_state *state)
{
	mutex_enter(&state->mutex);
	require_mutex_not_held(state);
	mutex_exit(&state->mutex);
}

static void
call_assert_after_acquire(struct assertion_requirement_state *state)
{
	assert_after_acquire(state);
}

static void
call_assert_after_release(struct assertion_requirement_state *state)
{
	mutex_enter(&state->mutex);
	assert_after_release(state);
}

static void
call_assert_held_conditionally(struct assertion_requirement_state *state)
{
	assert_held_conditionally(state, 1);
}

static void
call_assert_held_unreachable(struct assertion_requirement_state *state)
{
	assert_held_unreachable(state);
}

static void
call_require_held_maybe(struct assertion_requirement_state *state, int held)
{
	if (held)
		mutex_enter(&state->mutex);
	require_mutex_held(state);
	ASSERT(MUTEX_NOT_HELD(&state->mutex));
}

static void
call_require_read_held(struct assertion_requirement_state *state)
{
	rw_rdlock(&state->rwlock);
	require_read_held(state);
	rw_exit(&state->rwlock);
}

static void
call_require_read_unheld(struct assertion_requirement_state *state)
{
	require_read_held(state);
}

static void
call_require_write_held(struct assertion_requirement_state *state)
{
	rw_wrlock(&state->rwlock);
	require_write_held(state);
	rw_exit(&state->rwlock);
}

static void
call_require_write_read_held(struct assertion_requirement_state *state)
{
	rw_rdlock(&state->rwlock);
	require_write_held(state);
	rw_exit(&state->rwlock);
}

static void
acquire_mutex(struct assertion_requirement_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->mutex))
	mutex_enter(&state->mutex);
}

static void
assert_after_callee_acquire(struct assertion_requirement_state *state)
{
	acquire_mutex(state);
	ASSERT(MUTEX_HELD(&state->mutex));
	mutex_exit(&state->mutex);
}

static void
call_assert_after_callee_acquire(struct assertion_requirement_state *state)
{
	assert_after_callee_acquire(state);
}
