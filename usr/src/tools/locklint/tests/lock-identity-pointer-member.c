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
 * Characterize mutex identities reached through pointer-valued structure
 * members.  Repeated evaluation and an equivalent saved outer pointer must
 * identify the same mutex, while an explicit member reassignment must keep
 * the old and new target objects distinct.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

struct inner_state {
	mutex_t lock;
};

struct outer_state {
	struct inner_state *inner;
};

struct request_state {
	struct outer_state *outer;
};

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

void repeated_pointer_member(struct outer_state *);
void nested_pointer_alias(struct request_state *);
void reassigned_pointer_member(struct outer_state *, struct inner_state *);

void
repeated_pointer_member(struct outer_state *outer)
{
	mutex_enter(&outer->inner->lock);
	mutex_exit(&outer->inner->lock);
}

void
nested_pointer_alias(struct request_state *request)
{
	struct outer_state *outer = request->outer;

	mutex_enter(&request->outer->inner->lock);
	mutex_exit(&outer->inner->lock);
}

void
reassigned_pointer_member(struct outer_state *outer,
    struct inner_state *replacement)
{
	mutex_enter(&outer->inner->lock);
	outer->inner = replacement;
	mutex_exit(&outer->inner->lock);
}
