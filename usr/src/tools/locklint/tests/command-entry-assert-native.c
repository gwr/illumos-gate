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
 * Verify command-declared function-entry lock assertions.  Helpers preserve
 * their required entry state so callers isolate satisfied, unsatisfied, and
 * conditional requirements without adding return effects.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;

struct command_assert_nested {
	mutex_t lock;
};

struct command_assert_state {
	mutex_t mutex;
	rwlock_t rwlock;
	struct command_assert_nested nested;
};

mutex_t command_assert_global_lock;

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void rw_rdlock(rwlock_t *);
extern void rw_wrlock(rwlock_t *);
extern void rw_exit(rwlock_t *);

static void
command_assert_mutex(struct command_assert_state *state)
{
	mutex_exit(&state->mutex);
	mutex_enter(&state->mutex);
}

static void
command_assert_mutex_second(struct command_assert_state *state)
{
	(void) state;
}

static void
command_assert_direct(mutex_t *lock)
{
	mutex_exit(lock);
	mutex_enter(lock);
}

static void
command_assert_nested(struct command_assert_state *state)
{
	mutex_exit(&state->nested.lock);
	mutex_enter(&state->nested.lock);
}

static void
command_assert_global(void)
{
	mutex_exit(&command_assert_global_lock);
	mutex_enter(&command_assert_global_lock);
}

static void
command_assert_read(struct command_assert_state *state)
{
	(void) state;
}

static void
command_assert_write(struct command_assert_state *state)
{
	(void) state;
}

static void
command_assert_rw(struct command_assert_state *state)
{
	(void) state;
}

void
command_assert_synthetic_root(struct command_assert_state *state)
{
	mutex_exit(&state->mutex);
	mutex_enter(&state->mutex);
}

void
command_assert_mutex_correct(struct command_assert_state *state)
{
	mutex_enter(&state->mutex);
	command_assert_mutex(state);
	mutex_exit(&state->mutex);
}

void
command_assert_mutex_incorrect(struct command_assert_state *state)
{
	command_assert_mutex(state);
}

void
command_assert_mutex_maybe(struct command_assert_state *state, int held)
{
	if (held)
		mutex_enter(&state->mutex);
	command_assert_mutex(state);
	if (held)
		mutex_exit(&state->mutex);
}

void
command_assert_second_correct(struct command_assert_state *state)
{
	mutex_enter(&state->mutex);
	command_assert_mutex_second(state);
	mutex_exit(&state->mutex);
}

void
command_assert_second_incorrect(struct command_assert_state *state)
{
	command_assert_mutex_second(state);
}

void
command_assert_direct_correct(mutex_t *lock)
{
	mutex_enter(lock);
	command_assert_direct(lock);
	mutex_exit(lock);
}

void
command_assert_direct_incorrect(mutex_t *lock)
{
	command_assert_direct(lock);
}

void
command_assert_nested_correct(struct command_assert_state *state)
{
	mutex_enter(&state->nested.lock);
	command_assert_nested(state);
	mutex_exit(&state->nested.lock);
}

void
command_assert_nested_incorrect(struct command_assert_state *state)
{
	command_assert_nested(state);
}

void
command_assert_global_correct(void)
{
	mutex_enter(&command_assert_global_lock);
	command_assert_global();
	mutex_exit(&command_assert_global_lock);
}

void
command_assert_global_incorrect(void)
{
	command_assert_global();
}

void
command_assert_read_reader(struct command_assert_state *state)
{
	rw_rdlock(&state->rwlock);
	command_assert_read(state);
	rw_exit(&state->rwlock);
}

void
command_assert_read_writer(struct command_assert_state *state)
{
	rw_wrlock(&state->rwlock);
	command_assert_read(state);
	rw_exit(&state->rwlock);
}

void
command_assert_read_unheld(struct command_assert_state *state)
{
	command_assert_read(state);
}

void
command_assert_write_writer(struct command_assert_state *state)
{
	rw_wrlock(&state->rwlock);
	command_assert_write(state);
	rw_exit(&state->rwlock);
}

void
command_assert_write_reader(struct command_assert_state *state)
{
	rw_rdlock(&state->rwlock);
	command_assert_write(state);
	rw_exit(&state->rwlock);
}

void
command_assert_write_unheld(struct command_assert_state *state)
{
	command_assert_write(state);
}

void
command_assert_rw_reader(struct command_assert_state *state)
{
	rw_rdlock(&state->rwlock);
	command_assert_rw(state);
	rw_exit(&state->rwlock);
}

void
command_assert_rw_writer(struct command_assert_state *state)
{
	rw_wrlock(&state->rwlock);
	command_assert_rw(state);
	rw_exit(&state->rwlock);
}

void
command_assert_rw_unheld(struct command_assert_state *state)
{
	command_assert_rw(state);
}
