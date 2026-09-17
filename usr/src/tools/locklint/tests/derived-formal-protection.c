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
 * Verify that protection requirements derived through a local pointer retain
 * their relationship to the callee's formal object and dynamic array index.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct derived_port {
	mutex_t lock;
	int value;
} derived_port_t;

typedef struct derived_state {
	derived_port_t *ports;
} derived_state_t;

_NOTE(MUTEX_PROTECTS_DATA(derived_port::lock, derived_port))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int derived_root(derived_state_t *, unsigned int);

static int
derived_helper(derived_state_t *state, unsigned int index)
{
	derived_port_t *port = &state->ports[index];
	int value;

	value = port->value;
	port->value = 0;
	return (value);
}

static int
derived_root(derived_state_t *state, unsigned int index)
{
	derived_port_t *port = &state->ports[index];
	int value;

	mutex_enter(&port->lock);
#ifdef DERIVED_FORMAL_DIFFERENT
	value = derived_helper(state, index ^ 1);
#else
	value = derived_helper(state, index);
#endif
	mutex_exit(&port->lock);
	return (value);
}
