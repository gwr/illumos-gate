/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

typedef struct command_type_mutex {
	int opaque;
} command_type_mutex_t;

typedef struct command_type_state {
	command_type_mutex_t lock;
	command_type_mutex_t alternate;
	long value;
	int source_value;
} command_type_state_t;
