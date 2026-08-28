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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Verify type-scoped protection for members promoted through valid inline
 * anonymous structures and unions.
 */

#define	_NOTE(arg)

typedef int mutex_t;

struct anonymous_struct_state {
	int padding;
	struct {
		mutex_t lock;
		int value;
	};
};

_NOTE(MUTEX_PROTECTS_DATA(anonymous_struct_state::lock,
    anonymous_struct_state::value))

struct nested_anonymous_state {
	int padding;
	union {
		int alternate;
		struct {
			int nested_padding;
			struct {
				mutex_t lock;
				int value;
			};
		};
	};
};

_NOTE(MUTEX_PROTECTS_DATA(nested_anonymous_state::lock,
    nested_anonymous_state::value))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
check_anonymous_struct(struct anonymous_struct_state *state)
{
	state->value = 1;
	mutex_enter(&state->lock);
	state->value = 2;
	mutex_exit(&state->lock);
}

static void
check_nested_anonymous_union(struct nested_anonymous_state *state)
{
	state->value = 1;
	mutex_enter(&state->lock);
	state->value = 2;
	mutex_exit(&state->lock);
}
