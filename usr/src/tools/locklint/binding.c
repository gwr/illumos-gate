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
 * Maintain function-owned canonical formal-to-actual binding environments.
 * Environments use compact immutable arrays because functions normally have
 * few object-bearing arguments and complete environments are context keys.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"
#include "binding.h"

static int
compare_binding(const struct formal_binding *left,
    const struct formal_binding *right)
{
	int result;

	if (left->argument < right->argument)
		return (-1);
	if (left->argument > right->argument)
		return (1);
	result = AVL_PCMP(left->actual_identity, right->actual_identity);
	return (result);
}

static int
compare_binding_qsort(const void *left, const void *right)
{
	return (compare_binding(left, right));
}

static int
compare_environment(const void *left_arg, const void *right_arg)
{
	const struct binding_environment *left = left_arg;
	const struct binding_environment *right = right_arg;
	size_t count = left->count < right->count ? left->count : right->count;
	size_t index;
	int result;

	for (index = 0; index < count; index++) {
		result = compare_binding(&left->entries[index],
		    &right->entries[index]);
		if (result != 0)
			return (result);
	}
	if (left->count < right->count)
		return (-1);
	if (left->count > right->count)
		return (1);
	return (0);
}

void
binding_collection_create(struct binding_environment_collection *collection)
{
	avl_create(&collection->environments, compare_environment,
	    sizeof (struct binding_environment),
	    offsetof(struct binding_environment, by_value));
}

void
binding_collection_free(struct binding_environment_collection *collection)
{
	struct binding_environment *environment;
	void *cookie = NULL;

	while ((environment = avl_destroy_nodes(&collection->environments,
	    &cookie)) != NULL)
		free(environment);
	avl_destroy(&collection->environments);
}

/*
 * Copy, sort, validate, and intern one complete environment.  Allocation or
 * validation failure leaves both output arguments unchanged.
 */
int
binding_environment_intern(struct binding_environment_collection *collection,
    const struct formal_binding *entries, size_t count,
    struct binding_environment **result, bool *existed)
{
	struct binding_environment *candidate;
	struct binding_environment *environment;
	avl_index_t where;
	size_t index;
	size_t size;

	if (count != 0 && entries == NULL)
		return (EINVAL);
	if (count > (SIZE_MAX - sizeof (*candidate)) / sizeof (*entries))
		return (EOVERFLOW);
	size = sizeof (*candidate) + count * sizeof (*entries);
	candidate = calloc(1, size);
	if (candidate == NULL)
		return (ENOMEM);
	candidate->count = count;
	if (count != 0) {
		(void) memcpy(candidate->entries, entries,
		    count * sizeof (*entries));
		qsort(candidate->entries, count, sizeof (*entries),
		    compare_binding_qsort);
	}
	for (index = 0; index < count; index++) {
		if (candidate->entries[index].actual_identity == NULL ||
		    (index != 0 &&
		    candidate->entries[index - 1].argument ==
		    candidate->entries[index].argument)) {
			free(candidate);
			return (EINVAL);
		}
	}
	environment = avl_find(&collection->environments, candidate, &where);
	if (environment != NULL) {
		free(candidate);
		*result = environment;
		*existed = true;
		return (0);
	}
	avl_insert(&collection->environments, candidate, where);
	*result = candidate;
	*existed = false;
	return (0);
}

size_t
binding_environment_count(
    struct binding_environment_collection *collection)
{
	return (avl_numnodes(&collection->environments));
}

size_t
binding_environment_entry_count(
    struct binding_environment_collection *collection)
{
	struct binding_environment *environment;
	size_t count = 0;

	for (environment = avl_first(&collection->environments);
	    environment != NULL;
	    environment = AVL_NEXT(&collection->environments, environment))
		count += environment->count;
	return (count);
}
