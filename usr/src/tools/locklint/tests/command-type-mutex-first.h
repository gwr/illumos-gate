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
 * Define the first origin of a type used to verify command-file pairing of
 * separately parsed, layout-equivalent lock and data members.
 */

typedef struct command_type_mutex {
	int opaque;
} command_type_mutex_t;

struct command_type_nested {
	int first;
	int second;
};

typedef struct command_type_state {
	command_type_mutex_t lock;
	command_type_mutex_t alternate;
	int value;
	struct command_type_nested nested;
	int source_value;
} command_type_state_t;

struct command_other_state {
	int value;
};
