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
 * Characterize historical command-file entry-lock assertions.  Each helper
 * immediately performs an operation that requires the asserted input mode,
 * while its callers provide valid and invalid entry states.
 */

#ifdef __lock_lint
#include <sys/mutex.h>
#include <sys/rwlock.h>
#else
#define	RW_READER	0
#define	RW_WRITER	1

typedef struct kmutex {
	int opaque;
} kmutex_t;

typedef struct krwlock {
	int opaque;
} krwlock_t;
#endif

#ifndef ENTRY_ASSERT_VARIANT
#define	ENTRY_ASSERT_VARIANT	0
#endif

struct entry_assert_state {
	kmutex_t mutex;
	krwlock_t rwlock;
};

kmutex_t entry_assert_global_mutex;

#ifndef __lock_lint
extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void rw_enter(krwlock_t *, int);
extern void rw_exit(krwlock_t *);
extern void rw_downgrade(krwlock_t *);
#endif

#if ENTRY_ASSERT_VARIANT == 1

static void
command_global_helper(void)
{
	mutex_exit(&entry_assert_global_mutex);
	mutex_enter(&entry_assert_global_mutex);
}

void
command_global_correct(void)
{
	mutex_enter(&entry_assert_global_mutex);
	command_global_helper();
	mutex_exit(&entry_assert_global_mutex);
}

void
command_global_incorrect(void)
{
	command_global_helper();
}

#elif ENTRY_ASSERT_VARIANT == 2

static void
command_mutex_helper(struct entry_assert_state *state)
{
	mutex_exit(&state->mutex);
	mutex_enter(&state->mutex);
}

void
command_mutex_correct(struct entry_assert_state *state)
{
	mutex_enter(&state->mutex);
	command_mutex_helper(state);
	mutex_exit(&state->mutex);
}

void
command_mutex_incorrect(struct entry_assert_state *state)
{
	command_mutex_helper(state);
}

#elif ENTRY_ASSERT_VARIANT == 3

static void
command_read_helper(struct entry_assert_state *state)
{
	(void) state;
}

void
command_read_correct(struct entry_assert_state *state)
{
	rw_enter(&state->rwlock, RW_READER);
	command_read_helper(state);
	rw_exit(&state->rwlock);
}

void
command_read_writer(struct entry_assert_state *state)
{
	rw_enter(&state->rwlock, RW_WRITER);
	command_read_helper(state);
	rw_exit(&state->rwlock);
}

void
command_read_incorrect(struct entry_assert_state *state)
{
	command_read_helper(state);
}

#elif ENTRY_ASSERT_VARIANT == 4

static void
command_write_helper(struct entry_assert_state *state)
{
	rw_downgrade(&state->rwlock);
	rw_exit(&state->rwlock);
	rw_enter(&state->rwlock, RW_WRITER);
}

void
command_write_correct(struct entry_assert_state *state)
{
	rw_enter(&state->rwlock, RW_WRITER);
	command_write_helper(state);
	rw_exit(&state->rwlock);
}

void
command_write_reader(struct entry_assert_state *state)
{
	rw_enter(&state->rwlock, RW_READER);
	command_write_helper(state);
	rw_exit(&state->rwlock);
}

void
command_write_unheld(struct entry_assert_state *state)
{
	command_write_helper(state);
}

#elif ENTRY_ASSERT_VARIANT == 5

static void
command_multiple_first(struct entry_assert_state *state)
{
	mutex_exit(&state->mutex);
	mutex_enter(&state->mutex);
}

static void
command_multiple_second(struct entry_assert_state *state)
{
	mutex_exit(&state->mutex);
	mutex_enter(&state->mutex);
}

void
command_multiple_correct(struct entry_assert_state *state)
{
	mutex_enter(&state->mutex);
	command_multiple_first(state);
	command_multiple_second(state);
	mutex_exit(&state->mutex);
}

void
command_multiple_incorrect(struct entry_assert_state *state)
{
	command_multiple_first(state);
	command_multiple_second(state);
}

#elif ENTRY_ASSERT_VARIANT == 6

static void
command_two_formal_helper(struct entry_assert_state *first,
    struct entry_assert_state *second)
{
	(void) first;
	(void) second;
}

void
command_two_formal_first(struct entry_assert_state *first,
    struct entry_assert_state *second)
{
	mutex_enter(&first->mutex);
	command_two_formal_helper(first, second);
	mutex_exit(&first->mutex);
}

void
command_two_formal_second(struct entry_assert_state *first,
    struct entry_assert_state *second)
{
	mutex_enter(&second->mutex);
	command_two_formal_helper(first, second);
	mutex_exit(&second->mutex);
}

void
command_two_formal_unheld(struct entry_assert_state *first,
    struct entry_assert_state *second)
{
	command_two_formal_helper(first, second);
}

#endif
