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
 * Copyright 2026 Gordon W. Ross
 */

#define	_NOTE(arg)

typedef struct mutex {
	int opaque;
} mutex_t;

struct policy_nested {
	int first;
	int second;
};

struct policy_state {
	mutex_t lock;
	int protected;
	int readable;
	int scheme;
	struct policy_nested scheme_group;
	int mutex_after_scheme;
	int read_only;
};

static struct policy_state policy_object;

_NOTE(DATA_READABLE_WITHOUT_LOCK(policy_state::readable))
_NOTE(MUTEX_PROTECTS_DATA(policy_state::lock,
    policy_state::{ protected readable scheme }))
_NOTE(SCHEME_PROTECTS_DATA("external convention", policy_state::scheme))
_NOTE(SCHEME_PROTECTS_DATA("aggregate convention",
    policy_state::scheme_group))
_NOTE(SCHEME_PROTECTS_DATA("initialization convention",
    policy_state::mutex_after_scheme))
_NOTE(MUTEX_PROTECTS_DATA(policy_state::lock,
    policy_state::mutex_after_scheme))
_NOTE(READ_ONLY_DATA(policy_state::read_only))
_NOTE(MUTEX_PROTECTS_DATA(policy_object.lock, policy_object.read_only))

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);

static int
check_data_policy(void)
{
	int value;

	value = policy_object.protected;
	policy_object.protected = value;

	value += policy_object.readable;
	policy_object.readable = value;

	value += policy_object.scheme;
	policy_object.scheme = value;

	value += policy_object.scheme_group.first;
	policy_object.scheme_group.second = value;

	value += policy_object.mutex_after_scheme;
	policy_object.mutex_after_scheme = value;

	mutex_enter(&policy_object.lock);
	value += policy_object.read_only;
	policy_object.read_only = value;
	mutex_exit(&policy_object.lock);

	mutex_enter(&policy_object.lock);
	value += policy_object.protected;
	policy_object.protected = value;
	mutex_exit(&policy_object.lock);

	return (value);
}
