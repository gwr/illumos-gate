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
 * Intern lock identities for one whole-program analysis.  Identity consists
 * only of the retained analysis object and its separate target coordinate;
 * descriptive metadata is retained on first insertion but is not a key.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "avl.h"
#include "lock_identity.h"

static int
compare_lock_identity(const void *left_arg, const void *right_arg)
{
	const struct lock_identity *left = left_arg;
	const struct lock_identity *right = right_arg;
	int result;

	result = AVL_PCMP(left->key.analysis_object,
	    right->key.analysis_object);
	if (result != 0)
		return (result);
	if (left->key.target_offset < right->key.target_offset)
		return (-1);
	if (left->key.target_offset > right->key.target_offset)
		return (1);
	return (0);
}

void
lock_identity_collection_create(struct lock_identity_collection *collection)
{
	avl_create(&collection->identities, compare_lock_identity,
	    sizeof (struct lock_identity), offsetof(struct lock_identity,
	    by_key));
}

void
lock_identity_collection_free(struct lock_identity_collection *collection)
{
	struct lock_identity *identity;
	void *cookie = NULL;

	while ((identity = avl_destroy_nodes(&collection->identities,
	    &cookie)) != NULL)
		free(identity);
	avl_destroy(&collection->identities);
}

/*
 * Return the canonical record for key.  The first insertion supplies its
 * non-key description.  Allocation failure leaves both outputs unchanged.
 */
int
lock_identity_intern(struct lock_identity_collection *collection,
    struct lock_identity_key key, enum lock_analysis_object_type object_type,
    struct lock_identity **result, bool *existed)
{
	struct lock_identity lookup = {
		.key = key
	};
	struct lock_identity *identity;
	avl_index_t where;

	if (key.analysis_object == NULL)
		return (EINVAL);
	identity = avl_find(&collection->identities, &lookup, &where);
	if (identity != NULL) {
		*result = identity;
		*existed = true;
		return (0);
	}
	identity = calloc(1, sizeof (*identity));
	if (identity == NULL)
		return (ENOMEM);
	identity->key = key;
	identity->analysis_object_type = object_type;
	avl_insert(&collection->identities, identity, where);
	*result = identity;
	*existed = false;
	return (0);
}

struct lock_identity *
lock_identity_first(struct lock_identity_collection *collection)
{
	return (avl_first(&collection->identities));
}

struct lock_identity *
lock_identity_next(struct lock_identity_collection *collection,
    struct lock_identity *identity)
{
	return (AVL_NEXT(&collection->identities, identity));
}

size_t
lock_identity_count(struct lock_identity_collection *collection)
{
	return (avl_numnodes(&collection->identities));
}
