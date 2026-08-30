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
 * Compare a call through an exactly initialized static operations vector.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	void *_opaque[1];
} mutex_t;

typedef struct indirect_state {
	mutex_t lock;
	int value;
} indirect_state_t;

typedef struct indirect_ops {
	int (*read)(indirect_state_t *);
} indirect_ops_t;

_NOTE(MUTEX_PROTECTS_DATA(indirect_state::lock, indirect_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
read_value(indirect_state_t *state)
{
	return (state->value);
}

static const indirect_ops_t indirect_ops = {
	.read = read_value
};

static int
locked_indirect(indirect_state_t *state)
{
	int value;

	mutex_enter(&state->lock);
	value = indirect_ops.read(state);
	mutex_exit(&state->lock);
	return (value);
}

static int
unlocked_indirect(indirect_state_t *state)
{
	return (indirect_ops.read(state));
}
