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
 * Verify that protection auditing retains member loads whose lowered base is
 * a formal or local pointer after caller and alias composition prove the
 * pointee is shared.
 */

#ifdef __lock_lint
#include <sys/note.h>
#else
#define	_NOTE(arg)
#endif

#define	ASSERT(expr)	((void)((expr) || assfail("assertion")))

typedef struct audit_composed_mutex {
	int opaque;
} audit_composed_mutex_t;

struct audit_composed_outer;

struct audit_composed_state {
	unsigned int flags;
	struct audit_composed_outer *owner;
	void	(*callback)(void *);
	void	*argument;
};

struct audit_composed_outer {
	audit_composed_mutex_t lock;
	void *taskq;
	struct audit_composed_state first;
	struct audit_composed_state second;
};

static struct audit_composed_outer audit_composed_shared;

_NOTE(MUTEX_PROTECTS_DATA(audit_composed_outer::lock,
    audit_composed_outer))

extern void audit_composed_consume(void (*)(void *), void *);
extern int ddi_taskq_dispatch(void *, void (*)(void *), void *, int);
extern int mutex_owned(audit_composed_mutex_t *);
extern void mutex_enter(audit_composed_mutex_t *);
extern void mutex_exit(audit_composed_mutex_t *);
extern int assfail(const char *);

static void
audit_composed_first(void *argument)
{
	(void) argument;
}

static void
audit_composed_second(void *argument)
{
	(void) argument;
}

static void
audit_composed_formal(struct audit_composed_state *state)
{
	struct audit_composed_outer *outer = state->owner;
	int error;

	ASSERT(mutex_owned(&outer->lock));
	if ((state->flags & 1) != 0)
		assfail("flags");
	state->flags = 1;
	error = ddi_taskq_dispatch(outer->taskq, state->callback,
	    state->argument, 0);
	if (error != 0)
		audit_composed_consume(state->callback, state->argument);
}

void
audit_composed_root(void)
{
	struct audit_composed_outer *alias = &audit_composed_shared;

	alias->first.callback = audit_composed_first;
	alias->first.argument = &alias->first;
	alias->first.owner = alias;
	alias->second.callback = audit_composed_second;
	alias->second.argument = &alias->second;
	alias->second.owner = alias;

	_NOTE(COMPETING_THREADS_NOW)
	audit_composed_formal(&alias->first);
	audit_composed_formal(&alias->second);
	_NOTE(NO_COMPETING_THREADS_NOW)
}
