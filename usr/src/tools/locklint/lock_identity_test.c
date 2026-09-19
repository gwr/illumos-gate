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
 * Exercise whole-analysis lock identity interning without Sparse parsing or
 * semantic-state transitions.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "lock_identity.h"

static unsigned int failures;

static void
check(bool condition, const char *message)
{
	if (condition)
		return;
	(void) fprintf(stderr, "FAIL: %s\n", message);
	failures++;
}

static int
compare_key(struct lock_identity_key left, struct lock_identity_key right)
{
	uintptr_t left_object = (uintptr_t)left.analysis_object;
	uintptr_t right_object = (uintptr_t)right.analysis_object;

	if (left_object < right_object)
		return (-1);
	if (left_object > right_object)
		return (1);
	if (left.target_offset < right.target_offset)
		return (-1);
	if (left.target_offset > right.target_offset)
		return (1);
	return (0);
}

static void
test_identity_interning(void)
{
	struct lock_identity_collection collection;
	unsigned int objects[2];
	struct lock_identity_key first_key = {
		.analysis_object = &objects[0],
		.target_offset = 8
	};
	struct lock_identity_key other_offset_key = {
		.analysis_object = &objects[0],
		.target_offset = 16
	};
	struct lock_identity_key other_object_key = {
		.analysis_object = &objects[1],
		.target_offset = 8
	};
	struct lock_identity *first;
	struct lock_identity *same;
	struct lock_identity *other_offset;
	struct lock_identity *other_object;
	bool existed;
	int error;

	lock_identity_collection_create(&collection);

	error = lock_identity_intern(&collection, first_key,
	    LOCK_ANALYSIS_OBJECT_SYMBOL, &first, &existed);
	check(error == 0 && !existed, "create first lock identity");
	check(first->key.analysis_object == first_key.analysis_object,
	    "identity retains analysis object");
	check(first->key.target_offset == first_key.target_offset,
	    "identity retains target offset");
	check(first->analysis_object_type == LOCK_ANALYSIS_OBJECT_SYMBOL,
	    "identity retains first non-key description");

	error = lock_identity_intern(&collection, first_key,
	    LOCK_ANALYSIS_OBJECT_PSEUDO, &same, &existed);
	check(error == 0 && existed && same == first,
	    "equal key reuses canonical identity");
	check(same->analysis_object_type == LOCK_ANALYSIS_OBJECT_SYMBOL,
	    "non-key description does not change identity");

	error = lock_identity_intern(&collection, other_offset_key,
	    LOCK_ANALYSIS_OBJECT_SYMBOL, &other_offset, &existed);
	check(error == 0 && !existed && other_offset != first,
	    "different target offset has distinct identity");
	error = lock_identity_intern(&collection, other_object_key,
	    LOCK_ANALYSIS_OBJECT_SYMBOL, &other_object, &existed);
	check(error == 0 && !existed && other_object != first,
	    "different analysis object has distinct identity");
	check(lock_identity_count(&collection) == 3,
	    "collection has three identities");

	lock_identity_collection_free(&collection);
}

static void
test_identity_order(void)
{
	struct lock_identity_collection collection;
	unsigned int objects[2];
	struct lock_identity_key keys[] = {
		{ &objects[1], 9 },
		{ &objects[0], 20 },
		{ &objects[0], -4 },
		{ &objects[1], 1 }
	};
	struct lock_identity_key expected[4];
	struct lock_identity *identity;
	bool existed;
	size_t count = sizeof (keys) / sizeof (keys[0]);
	size_t index;
	size_t expected_index;
	int error;

	lock_identity_collection_create(&collection);
	for (index = 0; index < count; index++) {
		error = lock_identity_intern(&collection, keys[index],
		    LOCK_ANALYSIS_OBJECT_UNSPECIFIED, &identity, &existed);
		check(error == 0 && !existed, "create identity for ordering");
		expected[index] = keys[index];
	}
	for (index = 0; index < count; index++) {
		for (expected_index = index + 1; expected_index < count;
		    expected_index++) {
			struct lock_identity_key temporary;

			if (compare_key(expected[index],
			    expected[expected_index]) <= 0)
				continue;
			temporary = expected[index];
			expected[index] = expected[expected_index];
			expected[expected_index] = temporary;
		}
	}
	expected_index = 0;
	for (identity = lock_identity_first(&collection); identity != NULL;
	    identity = lock_identity_next(&collection, identity)) {
		check(expected_index < count, "identity order has no extras");
		if (expected_index < count) {
			check(compare_key(identity->key,
			    expected[expected_index]) == 0,
			    "identity enumeration follows key order");
		}
		expected_index++;
	}
	check(expected_index == count, "identity order includes every record");

	lock_identity_collection_free(&collection);
}

static void
test_invalid_identity(void)
{
	struct lock_identity_collection collection;
	struct lock_identity *identity = NULL;
	bool existed = true;
	int error;

	lock_identity_collection_create(&collection);
	error = lock_identity_intern(&collection,
	    (struct lock_identity_key){ 0 },
	    LOCK_ANALYSIS_OBJECT_UNSPECIFIED, &identity, &existed);
	check(error == EINVAL, "null analysis object is rejected");
	check(identity == NULL && existed,
	    "invalid key preserves output arguments");
	check(lock_identity_count(&collection) == 0,
	    "invalid key does not change collection");
	lock_identity_collection_free(&collection);
}

int
main(void)
{
	test_identity_interning();
	test_identity_order();
	test_invalid_identity();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
