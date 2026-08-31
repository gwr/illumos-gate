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
 * Exercise protection that depends on relationships between actual
 * arguments.  Inside a callee, data and owner are distinct formal symbols,
 * so the callee cannot decide whether owner->lock protects data->value.
 * Locklint must retain that conditional relationship and evaluate it after
 * mapping both formals to actual arguments.
 *
 * The default variant passes state for every related formal.  Defining
 * FORMAL_ALIAS_DIFFERENT substitutes other where the relationship is meant
 * to fail.  The same source therefore exercises both sides without placing
 * both relationships in one analysis.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct formal_state {
	mutex_t lock;
	int value;
} formal_state_t;

_NOTE(MUTEX_PROTECTS_DATA(formal_state::lock, formal_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int formal_alias_root(formal_state_t *, formal_state_t *);

/*
 * Establish the basic relationship: owner->lock protects the access only
 * when data and owner denote the same formal_state instance.
 */
static int
formal_alias_callee(formal_state_t *data, formal_state_t *owner)
{
	int value;

	mutex_enter(&owner->lock);
	value = data->value;
	mutex_exit(&owner->lock);
	return (value);
}

/*
 * Preserve the relationship through an otherwise transparent caller.  This
 * prevents an implementation from succeeding only by inspecting the direct
 * call made by formal_alias_root().
 */
static int
formal_alias_wrapper(formal_state_t *data, formal_state_t *owner)
{
	return (formal_alias_callee(data, owner));
}

/*
 * Create two independent protection requirements for the same data and
 * ordinary required lock.  Each access has a different conditionally
 * satisfying formal lock.  The conditions must remain separate: combining
 * their alternatives would let first satisfy the access protected only by
 * second.
 */
static int
formal_alias_two_locks(formal_state_t *data, formal_state_t *first,
    formal_state_t *second)
{
	int value;

	mutex_enter(&first->lock);
	value = data->value;
	mutex_exit(&first->lock);
	mutex_enter(&second->lock);
	value += data->value;
	mutex_exit(&second->lock);
	return (value);
}

/*
 * Supply the actual-argument relationships for the direct, wrapped, and
 * independent-condition cases above.  This is the only analysis root, so all
 * conclusions about the helper formals must come from these calls.
 */
static int
formal_alias_root(formal_state_t *state, formal_state_t *other)
{
	int value;

#ifdef FORMAL_ALIAS_DIFFERENT
	value = formal_alias_callee(state, other);
	value += formal_alias_wrapper(state, other);
	value += formal_alias_two_locks(state, state, other);
#else
	value = formal_alias_callee(state, state);
	value += formal_alias_wrapper(state, state);
	value += formal_alias_two_locks(state, state, state);
#endif
	return (value);
}
