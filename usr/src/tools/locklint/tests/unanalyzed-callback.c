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
 * Distinguish unresolved callback exports from ordinary unresolved calls.
 * Each unresolved function has exactly one kind of use except for the
 * callback-and-call case, which tests which use controls OSLL's warning.
 */

struct unanalyzed_callback_ops {
	int (*callback)(void);
};

extern int unanalyzed_ops_only(void);
extern int unanalyzed_pointer_only(void);
extern int unanalyzed_ops_and_call(void);
extern int unanalyzed_call_only(void);
extern struct unanalyzed_callback_ops unanalyzed_ops;
extern struct unanalyzed_callback_ops unanalyzed_called_ops;
extern struct unanalyzed_callback_ops unanalyzed_resolved_ops;
extern int (*unanalyzed_pointer)(void);
extern int (*unanalyzed_duplicate_pointer)(void);
void unanalyzed_callback_root(void);

struct unanalyzed_callback_ops unanalyzed_ops = {
	unanalyzed_ops_only
};

int (*unanalyzed_pointer)(void) = unanalyzed_pointer_only;

int (*unanalyzed_duplicate_pointer)(void) = unanalyzed_ops_only;

struct unanalyzed_callback_ops unanalyzed_called_ops = {
	unanalyzed_ops_and_call
};

static int
unanalyzed_resolved_callback(void)
{
	return (0);
}

struct unanalyzed_callback_ops unanalyzed_resolved_ops = {
	unanalyzed_resolved_callback
};

void
unanalyzed_callback_root(void)
{
	(void) unanalyzed_ops_and_call();
	(void) unanalyzed_call_only();
}
