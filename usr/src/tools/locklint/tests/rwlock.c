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
 * Test local readers-writer lock policy, state, operations, and assertions.
 */

#define	_NOTE(arg)
#define	ASSERT(expr)
#define	RW_READ_HELD(lock)	rw_read_held(lock)
#define	RW_WRITE_HELD(lock)	rw_write_held(lock)
#define	RW_LOCK_HELD(lock)	rw_lock_held(lock)

typedef struct rwlock {
	void *_opaque[1];
} rwlock_t;

typedef enum rw_type {
	RW_WRITER,
	RW_READER,
	RW_READER_STARVEWRITER
} rw_type_t;

typedef struct rwlock_state {
	rwlock_t lock;
	int value;
	int readable;
	int scheme;
} rwlock_state_t;

_NOTE(RWLOCK_PROTECTS_DATA(rwlock_state::lock,
    rwlock_state::{ value readable }))
_NOTE(DATA_READABLE_WITHOUT_LOCK(rwlock_state::readable))
_NOTE(SCHEME_PROTECTS_DATA("external", rwlock_state::scheme))

extern void rw_enter(rwlock_t *, rw_type_t);
extern void rw_exit(rwlock_t *);
extern int rw_rdlock(rwlock_t *);
extern int rw_wrlock(rwlock_t *);
extern int rw_unlock(rwlock_t *);
extern int rw_read_held(rwlock_t *);
extern int rw_write_held(rwlock_t *);
extern int rw_lock_held(rwlock_t *);

static int
check_kernel_modes(rwlock_state_t *state)
{
	int value;

	value = state->value;
	state->value = 1;
	value += state->readable;
	state->readable = 1;
	state->scheme = 1;

	rw_enter(&state->lock, RW_READER);
	value += state->value;
	state->value = 2;
	rw_exit(&state->lock);

	rw_enter(&state->lock, RW_WRITER);
	value += state->value;
	state->value = 3;
	rw_exit(&state->lock);

	return (value);
}

static void
check_user_modes(rwlock_state_t *state)
{
	(void) rw_rdlock(&state->lock);
	(void) state->value;
	state->value = 1;
	(void) rw_unlock(&state->lock);

	(void) rw_wrlock(&state->lock);
	(void) state->value;
	state->value = 2;
	(void) rw_unlock(&state->lock);
}

static void
check_unknown_mode(rwlock_state_t *state, rw_type_t mode)
{
	rw_enter(&state->lock, mode);
	state->value = 1;
}

static int
check_mode_merge(rwlock_state_t *state, int writer)
{
	int value;

	if (writer)
		rw_enter(&state->lock, RW_WRITER);
	else
		rw_enter(&state->lock, RW_READER);
	value = state->value;
	state->value = 1;
	rw_exit(&state->lock);
	return (value);
}

static int
check_unheld_merge(rwlock_state_t *state, int take)
{
	int value;

	if (take)
		rw_enter(&state->lock, RW_READER_STARVEWRITER);
	value = state->value;
	return (value);
}

static void
check_invalid_operations(rwlock_state_t *state)
{
	rw_exit(&state->lock);

	rw_enter(&state->lock, RW_READER);
	rw_enter(&state->lock, RW_READER);
	rw_exit(&state->lock);

	rw_enter(&state->lock, RW_READER);
	rw_enter(&state->lock, RW_WRITER);
	rw_exit(&state->lock);
}

static int
check_read_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(RW_READ_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}

static int
check_write_assertion(rwlock_state_t *state)
{
	ASSERT(RW_WRITE_HELD(&state->lock));
	state->value = 1;
	return (state->value);
}

static int
check_lock_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(RW_LOCK_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}

static void
check_not_held_assertion(rwlock_state_t *state)
{
	rw_enter(&state->lock, RW_WRITER);
	ASSERT(!RW_LOCK_HELD(&state->lock));
	rw_enter(&state->lock, RW_READER);
	rw_exit(&state->lock);
}

static int
check_not_read_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(!RW_READ_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}

static int
check_not_write_assertion(rwlock_state_t *state)
{
	int value;

	ASSERT(!RW_WRITE_HELD(&state->lock));
	value = state->value;
	state->value = 1;
	return (value);
}
