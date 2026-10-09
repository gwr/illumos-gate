/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Verify that protection-audit datum identity stops at pointer-member
 * boundaries.  Aggregate pointees use their own structural identity, while
 * scalar pointees without a stable shared identity are not attributed to the
 * containing structure.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

struct audit_inner {
	int value;
};

struct audit_outer {
	struct audit_inner *inner;
	unsigned char *payload;
};

int
pointer_boundary_audit(struct audit_outer *outer)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	value = outer->inner->value;
	outer->inner->value = value + 1;
	value += outer->payload[0];
	outer->payload[1] = (unsigned char)value;
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}
