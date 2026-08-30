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
 * Test interprocedural readers-writer lock conditions and effects.
 */

#define	_NOTE(arg)

typedef struct rwlock {
	void *_opaque[1];
} rwlock_t;

typedef enum rw_type {
	RW_WRITER,
	RW_READER
} rw_type_t;

typedef struct rwlock_call_state {
	rwlock_t lock;
	int value;
} rwlock_call_state_t;

_NOTE(RWLOCK_PROTECTS_DATA(rwlock_call_state::lock,
    rwlock_call_state::value))

extern void rw_enter(rwlock_t *, rw_type_t);
extern void rw_exit(rwlock_t *);

static int
read_value(rwlock_call_state_t *state)
{
	return (state->value);
}

static void
write_value(rwlock_call_state_t *state)
{
	state->value = 1;
}

static int
call_read_as_reader(rwlock_call_state_t *state)
{
	int value;

	rw_enter(&state->lock, RW_READER);
	value = read_value(state);
	rw_exit(&state->lock);
	return (value);
}

static void
call_write_as_reader(rwlock_call_state_t *state)
{
	rw_enter(&state->lock, RW_READER);
	write_value(state);
	rw_exit(&state->lock);
}

static int
call_read_as_writer(rwlock_call_state_t *state)
{
	int value;

	rw_enter(&state->lock, RW_WRITER);
	value = read_value(state);
	rw_exit(&state->lock);
	return (value);
}

static void
call_write_as_writer(rwlock_call_state_t *state)
{
	rw_enter(&state->lock, RW_WRITER);
	write_value(state);
	rw_exit(&state->lock);
}

static void
acquire_reader(rwlock_call_state_t *state)
{
	rw_enter(&state->lock, RW_READER);
}

static void
acquire_writer(rwlock_call_state_t *state)
{
	rw_enter(&state->lock, RW_WRITER);
}

static void
release_lock(rwlock_call_state_t *state)
{
	rw_exit(&state->lock);
}

static int
check_reader_effect(rwlock_call_state_t *state)
{
	int value;

	acquire_reader(state);
	value = state->value;
	state->value = 1;
	release_lock(state);
	return (value);
}

static void
check_writer_effect(rwlock_call_state_t *state)
{
	acquire_writer(state);
	state->value = 1;
	release_lock(state);
}
