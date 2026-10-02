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
 * vector supplied outside the analysis.  The ordinary targets wait while
 * holding different locks.  A separate variant returns with different locks
 * held to verify that each declared target contributes its caller state.
 */

#ifdef __lock_lint
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/note.h>
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

#if COMMAND_TARGETS_SOURCE_REPRESENTATIVE
_NOTE(DECLARE_CONTRACT(command_target_ops::start,
    command_target_representative))
#endif

extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void cv_wait(condvar_t *, mutex_t *);
extern void command_target_stop(void) __attribute__((noreturn));
void command_target_incompatible(int);

static void
command_target_first(struct command_target_state *state)
{
#if COMMAND_TARGETS_DIFFERENT_EFFECTS
	mutex_enter(&state->first);
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->first))
#else
	mutex_enter(&state->first);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	mutex_exit(&state->first);
#endif
}

#if COMMAND_TARGETS_BOTH || COMMAND_TARGETS_DIFFERENT_EFFECTS
static void
command_target_second(struct command_target_state *state)
{
#if COMMAND_TARGETS_DIFFERENT_EFFECTS
	mutex_enter(&state->second);
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->second))
#else
	mutex_enter(&state->second);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	mutex_exit(&state->second);
#endif
}
#endif

void
command_target_incompatible(int value)
{
	(void) value;
}

static void
#if COMMAND_TARGETS_DIFFERENT_EFFECTS
command_targets_effects(struct command_target_ops *ops,
    struct command_target_state *state)
{
	ops->start(state);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	mutex_exit(&state->first);
	mutex_exit(&state->second);
	command_target_stop();
}
#else
command_targets_start(struct command_target_ops *ops,
    struct command_target_state *state)
{
#if COMMAND_TARGETS_EXPLICIT_DEREFERENCE
	(*ops->start)(state);
#else
	ops->start(state);
#endif
	command_target_stop();
}
#endif

static void
command_targets_finish(struct command_target_ops *ops,
    struct command_target_state *state)
{
	ops->finish(state);
	command_target_stop();
}
