/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify that an external protection scheme remains visible without treating
 * incidental mutex observations as protection failures.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct scheme_audit_state {
	mutex_t lock;
	int value;
	int locked;
} scheme_audit_state_t;

_NOTE(SCHEME_PROTECTS_DATA("external convention",
    scheme_audit_state::{ value locked }))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

void
external_scheme_audit(scheme_audit_state_t *state)
{
	_NOTE(COMPETING_THREADS_NOW)
	state->value = 1;
	mutex_enter(&state->lock);
	state->value = 2;
	state->locked = 1;
	mutex_exit(&state->lock);
	_NOTE(NO_COMPETING_THREADS_NOW)
}
