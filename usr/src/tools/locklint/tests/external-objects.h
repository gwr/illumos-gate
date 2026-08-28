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

#ifndef TEST_EXTERNAL_OBJECTS_H
#define	TEST_EXTERNAL_OBJECTS_H

#define	_NOTE(arg)

typedef int mutex_t;

struct external_state {
	mutex_t lock;
	int value;
};

struct external_data {
	int global_value;
	int member_value;
	int redeclared_value;
	int function_value;
	int hidden_function_value;
	int block_function_value;
};

struct external_lock_holder {
	mutex_t lock;
};

extern struct external_state external_object;
extern mutex_t external_global_lock;
extern struct external_lock_holder external_lock_holder;

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern int read_global_value(struct external_data *);
extern int read_member_value(struct external_data *);
extern int read_redeclared_value(struct external_data *);

#endif /* TEST_EXTERNAL_OBJECTS_H */
