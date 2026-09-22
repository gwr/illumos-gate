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
 * Define shared nested and containing types for cross-database policy
 * precedence characterization.
 */

#ifndef SCHEME_MUTEX_PRECEDENCE_H
#define	SCHEME_MUTEX_PRECEDENCE_H

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

typedef struct mutex {
	int opaque;
} mutex_t;

struct precedence_inner {
	int value;
};

struct precedence_outer {
	mutex_t lock;
	struct precedence_inner inner;
};

#endif /* SCHEME_MUTEX_PRECEDENCE_H */
