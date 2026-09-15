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
 * Verify that an unprotected-access diagnostic identifies unrelated locks
 * held at the access or at a failing caller.  Definite and path-dependent
 * local states retain their acquisition locations.
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct rwlock {
	int opaque;
} rwlock_t;

struct reporting_state {
	mutex_t required;
	mutex_t definite;
	mutex_t possible;
	mutex_t caller;
	mutex_t second;
	rwlock_t required_writer;
	int value;
	int write_value;
};

_NOTE(MUTEX_PROTECTS_DATA(reporting_state::required,
    reporting_state::value))
_NOTE(RWLOCK_PROTECTS_DATA(reporting_state::required_writer,
    reporting_state::write_value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern int rw_rdlock(rwlock_t *);
extern int rw_unlock(rwlock_t *);

static int
read_value(struct reporting_state *state)
{
	return (state->value);
}

static int
direct_wrong_lock(struct reporting_state *state)
{
	int value;

	mutex_enter(&state->definite);
	value = state->value;
	mutex_exit(&state->definite);
	return (value);
}

static int
multiple_wrong_locks(struct reporting_state *state)
{
	int value;

	mutex_enter(&state->definite);
	mutex_enter(&state->second);
	value = state->value;
	mutex_exit(&state->second);
	mutex_exit(&state->definite);
	return (value);
}

static void
required_wrong_mode(struct reporting_state *state)
{
	(void) rw_rdlock(&state->required_writer);
	state->write_value = 1;
	(void) rw_unlock(&state->required_writer);
}

static int
possible_wrong_lock(struct reporting_state *state, int take_lock)
{
	int value;

	if (take_lock)
		mutex_enter(&state->possible);
	value = state->value;
	if (take_lock)
		mutex_exit(&state->possible);
	return (value);
}

static int
caller_wrong_lock(struct reporting_state *state)
{
	int value;

	mutex_enter(&state->caller);
	value = read_value(state);
	mutex_exit(&state->caller);
	return (value);
}

static void
assumed_value(struct reporting_state *state)
{
	_NOTE(ASSUMING_PROTECTED(state->value))
}

static void
explicit_wrong_lock(struct reporting_state *state)
{
	mutex_enter(&state->caller);
	assumed_value(state);
	mutex_exit(&state->caller);
}
