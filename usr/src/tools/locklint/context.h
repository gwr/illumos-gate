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
#include <stdint.h>
#include <sys/queue.h>

#include "avl.h"

struct binding_environment;
struct basic_block;
struct context_exit;
struct continuation;
struct function_info;
struct instruction;
struct lock_identity;
struct provenance_edge;

#define	LOCKLINT_MAX_TRACKED_LOCKS	100

SLIST_HEAD(context_exit_list, context_exit);

/*
 * Lock sets and semantic states are immutable after insertion.  Lock sets
 * are separately interned so future semantic dimensions can share them.
 */
struct semantic_lock_state {
	const struct lock_identity *lock;
	unsigned int modes;
};

struct semantic_lock_set {
	avl_node_t by_value;
	size_t count;
	struct semantic_lock_state entries[];
};

struct competition_interval {
	int64_t minimum;
	int64_t maximum;
	bool minimum_unbounded;
	bool maximum_unbounded;
	bool entry_condition;
};

struct semantic_state {
	const struct semantic_lock_set *locks;
	struct competition_interval competition;
	avl_node_t by_value;
};

typedef bool (*context_lock_filter_f)(const struct lock_identity *, void *);

/*
 * A point identifies the next Sparse instruction to evaluate.  A NULL
 * instruction identifies the exit of the specified basic block.
 */
struct analysis_point {
	struct basic_block *block;
	struct instruction *next_instruction;
};

struct point_state;

/*
 * A context key consists only of semantic inputs.  Bindings and states must
 * be canonical before insertion.  Provenance and traversal state belong to
 * the context but never participate in this key.
 */
struct function_context {
	struct function_info *function;
	const struct binding_environment *bindings;
	const struct semantic_state *entry_state;
	avl_tree_t point_states;
	struct context_exit_list exits;
	avl_tree_t continuations;
	avl_tree_t provenance_edges;
	unsigned int exit_generation;
	avl_node_t by_key;
};

/*
 * A point state records one semantic state which has reached one analysis
 * point in a function context.  Queue linkage is embedded so duplicate
 * enqueue can be suppressed without searching the worklist.
 */
struct point_state {
	struct function_context *context;
	struct analysis_point point;
	const struct semantic_state *state;
	bool queued;
	STAILQ_ENTRY(point_state) work_link;
	avl_node_t by_key;
};

/*
 * Each function directly owns its contexts and interned semantic states.
 */
struct function_context_collection {
	avl_tree_t contexts;
	avl_tree_t lock_sets;
	avl_tree_t semantic_states;
};

void context_collection_create(struct function_info *);
void context_collection_free(struct function_info *);

int context_empty_state_intern(struct function_info *,
    struct semantic_state **, bool *);
int context_state_import(struct function_info *, const struct semantic_state *,
    struct semantic_state **, bool *);
int context_state_map_exit(struct function_info *,
    const struct semantic_state *, const struct semantic_state *,
    const struct semantic_state *, context_lock_filter_f, void *,
    struct semantic_state **, bool *);
/*
 * Lock identities must be canonical and stable for the function collection's
 * lifetime.  Zero modes removes the lock.
 */
int context_state_set_lock(struct function_info *,
    const struct semantic_state *, const struct lock_identity *, unsigned int,
    struct semantic_state **, bool *);
int context_state_set_competition(struct function_info *,
    const struct semantic_state *, struct competition_interval,
    struct semantic_state **, bool *);
int context_state_adjust_competition(struct function_info *,
    const struct semantic_state *, int, struct semantic_state **, bool *);
int context_create(struct function_info *,
    const struct binding_environment *, const struct semantic_state *,
    struct function_context **, bool *);
int context_point_state_record(struct function_context *, struct analysis_point,
    const struct semantic_state *, struct point_state **, bool *);

size_t context_count(struct function_info *);
size_t context_lock_set_count(struct function_info *);
size_t context_state_count(struct function_info *);
size_t context_state_lock_count(const struct semantic_state *);
unsigned int context_state_lock_modes(const struct semantic_state *,
    const struct lock_identity *);
struct competition_interval context_state_competition(
    const struct semantic_state *);
size_t context_point_state_count(struct function_context *);

#endif /* CONTEXT_H */
