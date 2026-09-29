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
 * Verify command-file targets for a function-pointer member of an operation
 * vector supplied outside the analysis.  The two compatible targets wait
 * while holding different locks so singleton and multiple-target analysis
 * are visible without requiring equal target side effects.
 */

#ifdef __lock_lint
#include <sys/condvar.h>
#include <sys/mutex.h>
typedef kmutex_t mutex_t;
typedef kcondvar_t condvar_t;
#else
#define	_NOTE(arg)
typedef struct mutex {
	int opaque;
} mutex_t;

typedef struct condvar {
	int opaque;
} condvar_t;
#endif

struct command_target_state {
	mutex_t first;
	mutex_t second;
	mutex_t wait;
	condvar_t cv;
};

typedef void (*command_target_t)(struct command_target_state *);

struct command_target_ops {
	command_target_t start;
	command_target_t finish;
};

#if COMMAND_TARGETS_SOURCE_CONTRACT
_NOTE(DECLARE_CONTRACT(command_target_ops::finish, NO_LOCK_EFFECTS))
#endif

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void cv_wait(condvar_t *, mutex_t *);
extern void command_target_stop(void) __attribute__((noreturn));
void command_target_incompatible(int);

static void
command_target_first(struct command_target_state *state)
{
	mutex_enter(&state->first);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	mutex_exit(&state->first);
}

#if COMMAND_TARGETS_BOTH
static void
command_target_second(struct command_target_state *state)
{
	mutex_enter(&state->second);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	mutex_exit(&state->second);
}
#endif

void
command_target_incompatible(int value)
{
	(void) value;
}

static void
command_targets_start(struct command_target_ops *ops,
    struct command_target_state *state)
{
	ops->start(state);
	command_target_stop();
}

static void
command_targets_finish(struct command_target_ops *ops,
    struct command_target_state *state)
{
	ops->finish(state);
	command_target_stop();
}
