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
 * Define synchronization-function descriptors and their setup-time API
 * collection.
 */

#ifndef SYNC_API_H
#define	SYNC_API_H

#include <stddef.h>

#include "events.h"

#define	LOCKLINT_NO_ARGUMENT	((unsigned int)-1)

enum locklint_api_profile {
	LOCKLINT_API_ILLUMOS_KERNEL = 1 << 0,
	LOCKLINT_API_ILLUMOS_USER = 1 << 1
};

enum sync_family {
	SYNC_FAMILY_MUTEX,
	SYNC_FAMILY_RWLOCK,
	SYNC_FAMILY_COND_WAIT,
	SYNC_FAMILY_COUNT
};

struct sync_func {
	const char *name;
	enum sync_family family;
	enum locklint_lock_action action;
	enum locklint_lock_mode mode;
	unsigned int lock_argument;
	unsigned int mode_argument;
	unsigned int profiles;
};

int sync_api_init(void);
void sync_api_fini(void);
const struct sync_func *sync_api_find(const char *);
int sync_api_replace(enum sync_family, const struct sync_func *, size_t);

#endif /* SYNC_API_H */
