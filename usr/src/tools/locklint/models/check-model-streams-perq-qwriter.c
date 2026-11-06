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
 * Verify per-queue inner qwriter ownership for two independent instances.
 * The fixture supplies the separately tested shared-put and exclusive-service
 * entry states.  Queue-address shadow locks match the model's per-queue
 * identity without simulating queue storage or allocating runtime objects.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/synch.h>

struct perq_qwriter_state {
	krwlock_t perimeter;
	int before_data;
	int callback_data;
	int after_read;
	int after_write;
	int after_exit;
	int wrong_queue_data;
	int exclusive_after_data;
};

_NOTE(RWLOCK_PROTECTS_DATA(perq_qwriter_state::perimeter,
    perq_qwriter_state::{ before_data callback_data after_read after_write
    after_exit wrong_queue_data exclusive_after_data }))

static struct perq_qwriter_state first;
static struct perq_qwriter_state second;
static mblk_t message;
static kmutex_t caller_lock;
static int caller_data;
static int exclusive_caller_data;
extern krwlock_t locklint_inner;
static int module_data;

_NOTE(MUTEX_PROTECTS_DATA(caller_lock, caller_data))
_NOTE(MUTEX_PROTECTS_DATA(caller_lock, exclusive_caller_data))
_NOTE(RWLOCK_PROTECTS_DATA(locklint_inner, module_data))

static void
perq_qwriter_callback(queue_t *q, mblk_t *mp)
{
	struct perq_qwriter_state *state = (void *)q;

	state->callback_data = 1;
	caller_data = 1;
	module_data = 1;
}

static void
perq_qwriter_exclusive_callback(queue_t *q, mblk_t *mp)
{
	exclusive_caller_data = 1;
}

static void
perq_qwriter_wrong_callback(queue_t *q, mblk_t *mp)
{
	second.wrong_queue_data = 1;
}

/*
 * Shared entry can return reader-held after deferral or writer-held after
 * immediate upgrade.  Both outcomes protect reads, and one exit releases
 * either mode.  The writes before upgrade and after exit are controls.
 */
static int
perq_qwriter_shared_entry(struct perq_qwriter_state *state)
{
	int value;

	rw_enter(&state->perimeter, RW_READER);
	state->before_data = 1;
	qwriter((queue_t *)(void *)state, &message, perq_qwriter_callback,
	    PERIM_INNER);
	value = state->after_read;
	state->after_write = value;
	rw_exit(&state->perimeter);
	state->after_exit = 1;
	return (value);
}

/* Already-exclusive entry must preserve both its perimeter and caller mutex. */
static void
perq_qwriter_exclusive_entry(struct perq_qwriter_state *state)
{
	rw_enter(&state->perimeter, RW_WRITER);
	qwriter((queue_t *)(void *)state, &message,
	    perq_qwriter_exclusive_callback, PERIM_INNER);
	state->exclusive_after_data = 1;
	rw_exit(&state->perimeter);
}

void
perq_qwriter_root(void)
{
	mutex_enter(&caller_lock);
	(void) perq_qwriter_shared_entry(&first);
	(void) perq_qwriter_shared_entry(&second);
	perq_qwriter_exclusive_entry(&first);
	perq_qwriter_exclusive_entry(&second);

	rw_enter(&first.perimeter, RW_READER);
	qwriter((queue_t *)(void *)&first, &message,
	    perq_qwriter_wrong_callback, PERIM_INNER);
	rw_exit(&first.perimeter);
	mutex_exit(&caller_lock);
}
