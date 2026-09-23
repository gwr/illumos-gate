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
 * Characterize coherent callback families assigned to an allocated
 * operations object and returned by its setup function.  Each family uses
 * distinct targets so later dispatch must preserve the assignment grouping.
 */

#ifdef __lock_lint
#include <sys/note.h>
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

struct operation_state {
	mutex_t first;
	mutex_t second;
	mutex_t first_done;
	mutex_t second_done;
	mutex_t wait;
	condvar_t cv;
};

typedef void (*operation_t)(struct operation_state *);

struct operation_vector {
	operation_t start;
	operation_t finish;
};

struct unrelated_vector {
	operation_t start;
	operation_t finish;
};

extern struct operation_vector *allocate_operations(void);
extern struct unrelated_vector *allocate_unrelated(void);
extern void publish_operations(struct operation_vector *);
extern void mutex_enter(mutex_t *);
extern void mutex_exit(mutex_t *);
extern void cv_wait(condvar_t *, mutex_t *);
extern void operation_stop(void) __attribute__((noreturn));
void invoke_first_profile(struct operation_state *);
void invoke_second_profile(struct operation_state *);
void invoke_incomplete_profile(struct operation_state *);

static void
first_start(struct operation_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->first))
	mutex_enter(&state->first);
}

static void
first_finish(struct operation_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->first_done))
	mutex_enter(&state->first_done);
}

static void
second_start(struct operation_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->second))
	mutex_enter(&state->second);
}

static void
second_finish(struct operation_state *state)
{
	_NOTE(MUTEX_ACQUIRED_AS_SIDE_EFFECT(state->second_done))
	mutex_enter(&state->second_done);
}

static struct operation_vector *
make_first_operations(void)
{
	struct operation_vector *operations = allocate_operations();

	operations->start = first_start;
	operations->finish = first_finish;
	return (operations);
}

static struct operation_vector *
make_second_operations(void)
{
	struct operation_vector *operations = allocate_operations();

	operations->start = second_start;
	operations->finish = second_finish;
	return (operations);
}

static struct unrelated_vector *
make_unrelated_operations(void)
{
	struct unrelated_vector *operations = allocate_unrelated();

	operations->start = first_start;
	operations->finish = first_finish;
	return (operations);
}

static struct operation_vector *
make_incomplete_operations(void)
{
	struct operation_vector *operations = allocate_operations();

	operations->start = first_start;
	return (operations);
}

static struct operation_vector *
make_invalidated_operations(operation_t replacement)
{
	struct operation_vector *operations = allocate_operations();

	operations->start = first_start;
	operations->finish = first_finish;
	operations->finish = replacement;
	return (operations);
}

static struct operation_vector *
make_published_operations(void)
{
	struct operation_vector *operations = allocate_operations();

	operations->start = first_start;
	operations->finish = first_finish;
	publish_operations(operations);
	return (operations);
}

void
invoke_first_profile(struct operation_state *state)
{
	struct operation_vector *operations = make_first_operations();

	operations->start(state);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	operations->finish(state);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	operation_stop();
}

void
invoke_second_profile(struct operation_state *state)
{
	struct operation_vector *operations = make_second_operations();

	operations->start(state);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	operations->finish(state);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	operation_stop();
}

void
invoke_incomplete_profile(struct operation_state *state)
{
	struct operation_vector *operations = make_incomplete_operations();

	operations->start(state);
	mutex_enter(&state->wait);
	cv_wait(&state->cv, &state->wait);
	mutex_exit(&state->wait);
	operation_stop();
}
