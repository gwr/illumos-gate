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
 * Verify that a containing structure's data policy applies to a pointer
 * member but does not extend through that pointer to a separately allocated
 * pointee.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

struct pointer_policy_state {
	mutex_t lock;
	char *buffer;
};

_NOTE(MUTEX_PROTECTS_DATA(pointer_policy_state::lock,
    pointer_policy_state))
_NOTE(DATA_READABLE_WITHOUT_LOCK(pointer_policy_state::buffer))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

void check_pointer_member_policy(struct pointer_policy_state *, char *);

void
check_pointer_member_policy(struct pointer_policy_state *state, char *buffer)
{
	state->buffer[0] = 1;
	state->buffer = buffer;

	mutex_enter(&state->lock);
	state->buffer[1] = 2;
	state->buffer = buffer;
	mutex_exit(&state->lock);
}
