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
 * Observe current-thread writer ownership through an opaque pointer.  The
 * true path may use that fact without an acquisition or caller requirement.
 * Remote-owner queries and saved results after release remain conservative.
 */

#define	_NOTE(arg)
typedef struct krwlock { int opaque; } krwlock_t;
typedef struct kmutex { int opaque; } kmutex_t;
typedef struct kcondvar { int opaque; } kcondvar_t;
typedef enum krw { RW_WRITER, RW_READER } krw_t;
extern int rw_write_held(krwlock_t *);
extern int rw_read_held(krwlock_t *);
extern void rw_enter(krwlock_t *, krw_t);
extern void rw_exit(krwlock_t *);
extern void mutex_enter(kmutex_t *);
extern void mutex_exit(kmutex_t *);
extern void cv_wait(kcondvar_t *, kmutex_t *);

struct opaque_state {
	krwlock_t lock;
	int guarded_data;
	int false_data;
	int remote_data;
	int released_data;
};
_NOTE(RWLOCK_PROTECTS_DATA(opaque_state::lock,
    opaque_state::guarded_data, opaque_state::false_data,
    opaque_state::remote_data, opaque_state::released_data))

static kmutex_t wait_lock;
static kcondvar_t wait_cv;
static krwlock_t other_lock;
static int other_data;
static struct opaque_state concrete;
_NOTE(RWLOCK_PROTECTS_DATA(other_lock, other_data))

/* One lexical guard contains the entire exemption; false stays unknown. */
void
opaque_guard(struct opaque_state *state)
{
	if (rw_write_held(&state->lock)) {
		_NOTE(HELD_DURING_WAIT_EXEMPT_ON(state->lock))
		mutex_enter(&wait_lock);
		cv_wait(&wait_cv, &wait_lock);
		mutex_exit(&wait_lock);
		_NOTE(HELD_DURING_WAIT_EXEMPT_OFF(state->lock))
		state->guarded_data = 1;
	}
}

void
opaque_other(struct opaque_state *state)
{
	if (rw_write_held(&state->lock))
		other_data = 1;
}

void
opaque_false(struct opaque_state *state)
{
	if (!rw_write_held(&state->lock))
		state->false_data = 1;
}

void
opaque_remote_reader(struct opaque_state *state)
{
	if (rw_read_held(&state->lock))
		state->remote_data = 1;
}

/* Observed writer ownership must expose a real recursive acquisition. */
void
opaque_reentrant(struct opaque_state *state)
{
	if (rw_write_held(&state->lock)) {
		rw_enter(&state->lock, RW_WRITER);
		rw_exit(&state->lock);
	}
}

/* Truth observed before release must not restore ownership afterward. */
void
opaque_saved(struct opaque_state *state)
{
	if (rw_write_held(&state->lock)) {
		int held = rw_write_held(&state->lock);

		rw_exit(&state->lock);
		if (held)
			state->released_data = 1;
	}
}

/* An independent caller supplies no concrete object or ownership binding. */
void
opaque_entry(struct opaque_state *state)
{
	opaque_guard(state);
}

/* Concrete calls must neither acquire a lock nor require writer entry. */
void
opaque_known_calls(void)
{
	opaque_guard(&concrete);
	rw_enter(&concrete.lock, RW_READER);
	opaque_guard(&concrete);
	rw_exit(&concrete.lock);
	rw_enter(&concrete.lock, RW_WRITER);
	opaque_guard(&concrete);
	rw_exit(&concrete.lock);
}
