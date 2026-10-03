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

#ifndef _LOCKLINT_TEST_REPRESENTATIVE_CALL_H
#define	_LOCKLINT_TEST_REPRESENTATIVE_CALL_H

typedef struct mutex {
	int opaque;
} mutex_t;

struct representative_object {
	mutex_t lock;
};

typedef void (*representative_enter_t)(struct representative_object *);
typedef struct representative_object *(*representative_create_t)(void);

struct representative_ops {
	representative_enter_t enter;
	representative_create_t create;
};

void representative_enter(struct representative_object *);
struct representative_object *representative_create(void);
void concrete_enter(struct representative_object *);

#endif /* _LOCKLINT_TEST_REPRESENTATIVE_CALL_H */
