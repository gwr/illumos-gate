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

#ifndef CONTEXT_H
#define	CONTEXT_H

#include <stdbool.h>
#include <stddef.h>

#include "avl.h"

struct binding_environment;
struct function_info;

/*
 * Semantic states are immutable after insertion.  The first implementation
 * represents only the empty state; semantic fields will be added with the
 * corresponding checker behavior.
 */
struct semantic_state {
	avl_node_t by_value;
};

/*
 * A context key consists only of semantic inputs.  Bindings and states must
 * be canonical before insertion.  Provenance and traversal state belong to
 * the context but never participate in this key.
 */
struct function_context {
	struct function_info *function;
	const struct binding_environment *bindings;
	const struct semantic_state *entry_state;
	avl_node_t by_key;
};

/*
 * Each function directly owns its contexts and interned semantic states.
 */
struct function_context_collection {
	avl_tree_t contexts;
	avl_tree_t semantic_states;
};

void context_init(struct function_info *);
void context_fini(struct function_info *);

int state_get_empty(struct function_info *, struct semantic_state **, bool *);
int context_get(struct function_info *,
    const struct binding_environment *, const struct semantic_state *,
    struct function_context **, bool *);

size_t context_count(struct function_info *);
size_t state_count(struct function_info *);

#endif /* CONTEXT_H */
