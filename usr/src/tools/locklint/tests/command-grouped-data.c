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
 * Provide names for grouped command data-policy declarations.  Analysis
 * behavior is covered by the individual command-policy fixtures.
 */

typedef struct mutex {
	int opaque;
} kmutex_t;

typedef struct rwlock {
	int opaque;
} krwlock_t;

struct command_grouped_nested {
	int first;
	int second;
};

struct command_grouped_state {
	kmutex_t mutex;
	krwlock_t rwlock;
	int mutex_first;
	int mutex_second;
	int rw_first;
	int rw_second;
	int scheme_first;
	int scheme_second;
	struct command_grouped_nested nested;
};

struct command_grouped_other {
	int role_first;
	int role_second;
};

int command_grouped_global_first;
int command_grouped_global_second;
