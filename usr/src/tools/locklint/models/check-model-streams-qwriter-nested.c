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
 * Verify same-perimeter nested inner qwriter execution.  The outer callback
 * acquires a mutex that nested execution must retain, including when the
 * outer callback itself was deferred.  Entry states are supplied explicitly
 * so this test isolates nesting from framework dispatch and lifecycle.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

struct nested_state {
	krwlock_t perimeter;
	queue_t read_queue;
	queue_t write_queue;
	int nested_data;
	int outer_after;
	int after_read;
	int after_write;
	int exclusive_after;
	int after_exit;
};

static struct nested_state state;
static mblk_t message;
static kmutex_t caller_lock;
static kmutex_t callback_lock;
static int caller_data;
static int callback_data;

_NOTE(MUTEX_PROTECTS_DATA(caller_lock, caller_data))
_NOTE(MUTEX_PROTECTS_DATA(callback_lock, callback_data))

#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERMOD
extern krwlock_t locklint_inner;

_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, nested_state::{ nested_data
    outer_after after_read after_write exclusive_after after_exit }))

#define	NESTED_LOCK	(&locklint_inner)
#define	NESTED_QUEUE	(&state.write_queue)
#define	NESTED_OWNER(q)	(&state)
#else
_NOTE(RWLOCK_PROTECTS_DATA(nested_state::perimeter, nested_state::{
    nested_data outer_after after_read after_write exclusive_after
    after_exit }))

#define	NESTED_LOCK	(&state.perimeter)
#if (MODEL_FLAGS & (D_MTPERQ | D_MTQPAIR)) == D_MTPERQ
#define	NESTED_QUEUE	((queue_t *)(void *)&state)
#define	NESTED_OWNER(q)	((struct nested_state *)(void *)(q))
#else
#define	NESTED_QUEUE	(&state.write_queue)
#define	NESTED_OWNER(q) \
	((struct nested_state *)((char *)(q) - \
	    __builtin_offsetof(struct nested_state, write_queue)))
#endif
#endif

static void
nested_callback(queue_t *q, mblk_t *mp)
{
	struct nested_state *owner = NESTED_OWNER(q);

	owner->nested_data = 1;
	callback_data = 1;
}

/* Nested submission must neither defer nor drop the outer callback's claim. */
static void
outer_callback(queue_t *q, mblk_t *mp)
{
	struct nested_state *owner = NESTED_OWNER(q);

	mutex_enter(&callback_lock);
	qwriter(q, mp, nested_callback, PERIM_INNER);
	owner->outer_after = 1;
	mutex_exit(&callback_lock);
	caller_data = 1;
}

/* Check ownership after callback return and release for both entry modes. */
static void
nested_entry(int mode)
{
	int value;

	mutex_enter(&caller_lock);
	if (mode == RW_READER)
		rw_enter(NESTED_LOCK, RW_READER);
	else
		rw_enter(NESTED_LOCK, RW_WRITER);
	qwriter(NESTED_QUEUE, &message, outer_callback, PERIM_INNER);
	value = state.after_read;
	if (mode == RW_WRITER)
		state.exclusive_after = value;
	else
		state.after_write = value;
	rw_exit(NESTED_LOCK);
	state.after_exit = 1;
	mutex_exit(&caller_lock);
}

void
nested_root(void)
{
	nested_entry(RW_READER);
	nested_entry(RW_WRITER);
}
