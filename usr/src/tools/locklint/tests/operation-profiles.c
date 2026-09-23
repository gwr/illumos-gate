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

struct operation_state {
	int value;
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

static void
first_start(struct operation_state *state)
{
	state->value = 1;
}

static void
first_finish(struct operation_state *state)
{
	state->value = 2;
}

static void
second_start(struct operation_state *state)
{
	state->value = 3;
}

static void
second_finish(struct operation_state *state)
{
	state->value = 4;
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
