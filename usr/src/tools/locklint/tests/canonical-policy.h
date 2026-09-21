/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Define a shared type whose policy is declared in only one translation unit.
 */

#ifndef TEST_CANONICAL_POLICY_H
#define	TEST_CANONICAL_POLICY_H

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct canonical_policy_mutex {
	int opaque;
} canonical_policy_mutex_t;

struct canonical_policy_state {
	canonical_policy_mutex_t lock;
	int value;
	int global_value;
	int local_value;
};

extern canonical_policy_mutex_t canonical_policy_global_lock;
static canonical_policy_mutex_t canonical_policy_local_lock;
extern void mutex_enter(canonical_policy_mutex_t *);
extern void mutex_exit(canonical_policy_mutex_t *);
extern int canonical_policy_use(struct canonical_policy_state *);

_NOTE(MUTEX_PROTECTS_DATA(canonical_policy_global_lock,
    canonical_policy_state::global_value))
_NOTE(MUTEX_PROTECTS_DATA(canonical_policy_local_lock,
    canonical_policy_state::local_value))

#endif /* TEST_CANONICAL_POLICY_H */
