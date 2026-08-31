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
 * Characterize exact actual-argument identity at one call site.  Compiling
 * with FORMAL_ALIAS_DIFFERENT passes distinct objects instead.
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

static int
formal_alias_callee(formal_state_t *data, formal_state_t *owner)
{
	int value;

	mutex_enter(&owner->lock);
	value = data->value;
	mutex_exit(&owner->lock);
	return (value);
}

static int
formal_alias_root(formal_state_t *state, formal_state_t *other)
{
#ifdef FORMAL_ALIAS_DIFFERENT
	return (formal_alias_callee(state, other));
#else
	return (formal_alias_callee(state, state));
#endif
}
