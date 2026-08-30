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

#ifndef TEST_VISIBILITY_CROSS_H
#define	TEST_VISIBILITY_CROSS_H

#define	_NOTE(arg)

typedef int mutex_t;

struct visibility_cross_nested {
	int value;
};

struct visibility_cross_state {
	mutex_t lock;
	int value;
	struct visibility_cross_nested nested;
};

_NOTE(MUTEX_PROTECTS_DATA(visibility_cross_state::lock,
    visibility_cross_state::value))
_NOTE(MUTEX_PROTECTS_DATA(visibility_cross_state::lock,
    visibility_cross_state::nested.value))

extern struct visibility_cross_state visibility_cross_global;

extern void visibility_cross_make_invisible(
    struct visibility_cross_state *);
extern void visibility_cross_make_visible(struct visibility_cross_state *);
extern void visibility_cross_make_global_invisible(void);
extern void visibility_cross_make_global_visible(void);
extern void visibility_cross_make_nested_invisible(
    struct visibility_cross_state *);
extern void visibility_cross_make_nested_visible(
    struct visibility_cross_state *);

#endif /* TEST_VISIBILITY_CROSS_H */
