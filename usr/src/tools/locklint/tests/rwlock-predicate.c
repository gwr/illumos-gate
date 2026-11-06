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
 * Verify ordinary rwlock predicate branches against current ownership.
 * Impossible paths must disappear without inventing ownership.  Queries
 * which can observe another thread remain uncertain when locally unheld;
 * reachable unprotected writes and unresolved identities remain controls.
 */

#define	_NOTE(arg)

typedef struct krwlock {
	int opaque;
} krwlock_t;

typedef enum krw {
	RW_WRITER,
	RW_READER
} krw_t;

extern void rw_enter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
extern int rw_tryupgrade(krwlock_t *);
extern int rw_read_held(krwlock_t *);
extern int rw_write_held(krwlock_t *);
extern int rw_lock_held(krwlock_t *);
extern int _rw_read_held(void *);
extern int _rw_write_held(void *);

static krwlock_t lock;
static krwlock_t other;
static int impossible_data;
static int unheld_data;
static int remote_reader_data;
static int remote_reader_false_data;
static int remote_lock_data;
static int remote_lock_false_data;
static int reader_data;
static int other_data;
static int mixed_data;
static int saved_data;
static int unresolved_true_data;
static int unresolved_false_data;
static int protected_data;

_NOTE(RWLOCK_PROTECTS_DATA(other, impossible_data))
_NOTE(RWLOCK_PROTECTS_DATA(lock, unheld_data))
_NOTE(RWLOCK_PROTECTS_DATA(other, remote_reader_data))
_NOTE(RWLOCK_PROTECTS_DATA(other, remote_reader_false_data))
_NOTE(RWLOCK_PROTECTS_DATA(other, remote_lock_data))
_NOTE(RWLOCK_PROTECTS_DATA(other, remote_lock_false_data))
_NOTE(RWLOCK_PROTECTS_DATA(lock, reader_data))
_NOTE(RWLOCK_PROTECTS_DATA(other, other_data))
_NOTE(RWLOCK_PROTECTS_DATA(other, mixed_data))
_NOTE(RWLOCK_PROTECTS_DATA(lock, saved_data))
_NOTE(RWLOCK_PROTECTS_DATA(lock, unresolved_true_data))
_NOTE(RWLOCK_PROTECTS_DATA(lock, unresolved_false_data))
_NOTE(RWLOCK_PROTECTS_DATA(lock, protected_data))

void
predicate_unheld(void)
{
	if (rw_read_held(&lock))
		remote_reader_data = 1;
	else
		remote_reader_false_data = 1;
	if (rw_write_held(&lock))
		impossible_data = 2;
	if (rw_lock_held(&lock))
		remote_lock_data = 1;
	else
		remote_lock_false_data = 1;
	if (_rw_read_held(&lock))
		impossible_data = 1;
	if (_rw_write_held(&lock))
		impossible_data = 3;
	if (!rw_write_held(&lock))
		unheld_data = 1;
}

void
predicate_reader(void)
{
	rw_enter(&lock, RW_READER);
	if (rw_write_held(&lock) != 0)
		impossible_data = 4;
	if (rw_read_held(&lock) == 0)
		impossible_data = 5;
	if (!rw_lock_held(&lock))
		impossible_data = 6;
	if (rw_read_held(&lock))
		reader_data = 1;
	rw_exit(&lock);
}

static void
predicate_bound_writer(krwlock_t *query)
{
	if (!rw_write_held(query))
		impossible_data = 14;
}

void
predicate_writer(void)
{
	rw_enter(&lock, RW_WRITER);
	predicate_bound_writer(&lock);
	if (!rw_write_held(&lock))
		impossible_data = 7;
	if (rw_read_held(&lock))
		impossible_data = 8;
	if (rw_lock_held(&lock) == 0)
		impossible_data = 9;
	if (rw_write_held(&lock)) {
		protected_data = 1;
		other_data = 1;
	}
	rw_exit(&lock);
}

void
predicate_aliases(void)
{
	rw_enter(&lock, RW_READER);
	if (!_rw_read_held(&lock))
		impossible_data = 10;
	if (_rw_write_held(&lock))
		impossible_data = 11;
	rw_exit(&lock);
	rw_enter(&lock, RW_WRITER);
	if (_rw_read_held(&lock))
		impossible_data = 12;
	if (!_rw_write_held(&lock))
		impossible_data = 13;
	rw_exit(&lock);
}

/*
 * An ignored upgrade preserves both outcomes without granting the other lock.
 */
void
predicate_mixed(void)
{
	rw_enter(&lock, RW_READER);
	(void) rw_tryupgrade(&lock);
	if (rw_write_held(&lock))
		protected_data = 2;
	else
		mixed_data = 1;
	rw_exit(&lock);
}

/* Predicate truth is observed before release, not recomputed at the branch. */
void
predicate_saved(void)
{
	int held;

	rw_enter(&lock, RW_WRITER);
	held = rw_write_held(&lock);
	rw_exit(&lock);
	if (held)
		saved_data = 1;
}

void
predicate_unresolved(krwlock_t **locks, int index)
{
	if (rw_write_held(locks[index]))
		unresolved_true_data = 1;
	else
		unresolved_false_data = 1;
}
