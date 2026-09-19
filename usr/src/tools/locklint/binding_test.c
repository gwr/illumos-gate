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
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Exercise canonical formal-to-actual binding environments independently of
 * Sparse parsing, function contexts, and checker behavior.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "binding.h"

static unsigned int failures;

static void
check(bool condition, const char *message)
{
	if (condition)
		return;
	(void) fprintf(stderr, "FAIL: %s\n", message);
	failures++;
}

static void
test_interning(void)
{
	struct binding_environment_collection collection;
	unsigned int identities[2];
	const struct lock_identity *first =
	    (const struct lock_identity *)&identities[0];
	const struct lock_identity *second =
	    (const struct lock_identity *)&identities[1];
	struct formal_binding ordered[] = {
		{ .argument = 0, .actual_identity = first },
		{ .argument = 2, .actual_identity = second }
	};
	struct formal_binding reversed[] = {
		{ .argument = 2, .actual_identity = second },
		{ .argument = 0, .actual_identity = first }
	};
	struct formal_binding aliases[] = {
		{ .argument = 0, .actual_identity = first },
		{ .argument = 2, .actual_identity = first }
	};
	struct binding_environment *environment;
	struct binding_environment *same;
	struct binding_environment *alias_environment;
	struct binding_environment *empty;
	bool existed;
	int error;

	binding_collection_create(&collection);
	error = binding_environment_intern(&collection, NULL, 0, &empty,
	    &existed);
	check(error == 0 && !existed, "create empty binding environment");
	check(empty->count == 0, "empty binding environment has no entries");

	error = binding_environment_intern(&collection, ordered, 2,
	    &environment, &existed);
	check(error == 0 && !existed, "create binding environment");
	check(environment->count == 2, "binding environment has two entries");
	check(environment->entries[0].argument == 0 &&
	    environment->entries[1].argument == 2,
	    "binding environment is sorted by formal argument");

	error = binding_environment_intern(&collection, reversed, 2, &same,
	    &existed);
	check(error == 0 && existed && same == environment,
	    "binding insertion order reuses canonical environment");

	error = binding_environment_intern(&collection, aliases, 2,
	    &alias_environment, &existed);
	check(error == 0 && !existed,
	    "distinct formal alias relationship creates environment");
	check(alias_environment->entries[0].actual_identity ==
	    alias_environment->entries[1].actual_identity,
	    "binding environment preserves exact aliases");
	check(binding_environment_count(&collection) == 3,
	    "collection owns three canonical binding environments");
	check(binding_environment_entry_count(&collection) == 4,
	    "binding environments retain four entries");

	binding_collection_free(&collection);
}

static void
test_errors(void)
{
	struct binding_environment_collection collection;
	unsigned int identity;
	struct formal_binding duplicate[] = {
		{ .argument = 1,
		    .actual_identity = (const struct lock_identity *)&identity },
		{ .argument = 1,
		    .actual_identity = (const struct lock_identity *)&identity }
	};
	struct formal_binding missing = { .argument = 0 };
	struct binding_environment *environment = NULL;
	bool existed = true;
	int error;

	binding_collection_create(&collection);
	error = binding_environment_intern(&collection, duplicate, 2,
	    &environment, &existed);
	check(error == EINVAL, "duplicate formal binding is rejected");
	check(environment == NULL && existed,
	    "duplicate error preserves output arguments");
	error = binding_environment_intern(&collection, &missing, 1,
	    &environment, &existed);
	check(error == EINVAL, "missing actual identity is rejected");
	check(binding_environment_count(&collection) == 0,
	    "invalid bindings are not retained");

	binding_collection_free(&collection);
}

static void
test_collection_ownership(void)
{
	struct binding_environment_collection first;
	struct binding_environment_collection second;
	unsigned int identity;
	struct formal_binding binding = {
		.argument = 0,
		.actual_identity = (const struct lock_identity *)&identity
	};
	struct binding_environment *first_environment;
	struct binding_environment *second_environment;
	bool existed;
	int error;

	binding_collection_create(&first);
	binding_collection_create(&second);
	error = binding_environment_intern(&first, &binding, 1,
	    &first_environment, &existed);
	check(error == 0, "create first collection environment");
	error = binding_environment_intern(&second, &binding, 1,
	    &second_environment, &existed);
	check(error == 0 && !existed,
	    "second collection owns distinct environment");
	check(second_environment != first_environment,
	    "environments are interned per collection");

	binding_collection_free(&second);
	binding_collection_free(&first);
}

int
main(void)
{
	test_interning();
	test_errors();
	test_collection_ownership();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
