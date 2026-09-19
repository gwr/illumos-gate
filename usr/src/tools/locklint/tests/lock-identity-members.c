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
 * Characterize distinct lock-member identities under one analysis object.
 */

typedef struct mutex {
	int opaque;
} mutex_t;

struct lock_pair {
	mutex_t first;
	mutex_t second;
};

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static void
member_events(struct lock_pair *pair)
{
	mutex_enter(&pair->first);
	mutex_exit(&pair->first);
	mutex_enter(&pair->second);
	mutex_exit(&pair->second);
}

void lock_identity_members(struct lock_pair *);

void
lock_identity_members(struct lock_pair *pair)
{
	member_events(pair);
}
