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

#ifndef _LOCKLINT_TEST_CONTRACT_CONSISTENCY_H
#define	_LOCKLINT_TEST_CONTRACT_CONSISTENCY_H

typedef struct mutex {
	int opaque;
} mutex_t;

struct consistency_object {
	mutex_t lock;
};

typedef void (*consistency_function_t)(struct consistency_object *);

struct implicit_consistency_ops {
	consistency_function_t enter;
};

struct explicit_consistency_ops {
	consistency_function_t enter;
};

struct matching_consistency_ops {
	consistency_function_t enter;
};

struct conflicting_consistency_ops {
	consistency_function_t enter;
};

struct command_consistency_ops {
	consistency_function_t enter;
};

typedef struct consistency_object *(*consistency_create_function_t)(void);

struct implicit_return_consistency_ops {
	consistency_create_function_t create;
};

struct matching_return_consistency_ops {
	consistency_create_function_t create;
};

void consistency_acquire(struct consistency_object *);
void consistency_none(struct consistency_object *);
void consistency_representative(struct consistency_object *);
struct consistency_object *consistency_create(void);
struct consistency_object *consistency_create_representative(void);

#endif /* _LOCKLINT_TEST_CONTRACT_CONSISTENCY_H */
