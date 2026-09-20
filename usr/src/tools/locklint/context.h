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
#define	LOCKLINT_MAX_TRACKED_VISIBILITY	100

SLIST_HEAD(context_exit_list, context_exit);

/*
 * Lock sets, visibility sets, and semantic states are immutable after
 * insertion.  The component sets are separately interned so states can share
 * unchanged dimensions.
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

enum semantic_visibility {
	SEMANTIC_VISIBILITY_VISIBLE,
	SEMANTIC_VISIBILITY_INVISIBLE
};

struct visibility_region {
	const void *analysis_object;
	int64_t target_offset;
	uint64_t target_length;
};

struct semantic_visibility_state {
	struct visibility_region region;
	enum semantic_visibility visibility;
};

struct semantic_visibility_set {
	avl_node_t by_value;
	size_t count;
	struct semantic_visibility_state entries[];
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
	const struct semantic_visibility_set *visibility;
	struct competition_interval competition;
	avl_node_t by_value;
};

typedef bool (*context_lock_filter_f)(const struct lock_identity *, void *);
typedef bool (*context_visibility_map_f)(const struct visibility_region *,
    struct visibility_region *, void *);

/*
 * A point identifies the next Sparse instruction to evaluate.  A NULL
 * instruction identifies the exit of the specified basic block.  An optional
 * conditional instruction and outcome retain a call result until the
 * corresponding branch edge consumes it.
 */
struct analysis_point {
	struct basic_block *block;
	struct instruction *next_instruction;
	const struct instruction *conditional_instruction;
	bool conditional_nonzero;
};

struct point_state;

enum function_context_kind {
	FUNCTION_CONTEXT_CALLER,
	FUNCTION_CONTEXT_ROOT,
	FUNCTION_CONTEXT_EFFECT_CALLER,
	FUNCTION_CONTEXT_EFFECT_CONTRACT
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
	enum function_context_kind kind;
	avl_tree_t point_states;
	struct context_exit_list exits;
	avl_tree_t continuations;
	avl_tree_t provenance_edges;
	unsigned int exit_generation;
	bool statistics_caller_recovery_start;
	bool statistics_caller_recovery_visit;
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
	avl_tree_t visibility_sets;
	avl_tree_t semantic_states;
	size_t visibility_sets_created;
	size_t visibility_sets_reused;
};

void context_collection_create(struct function_info *);
void context_collection_free(struct function_info *);

int context_empty_state_intern(struct function_info *,
    struct semantic_state **, bool *);
int context_entry_state_intern(struct function_info *,
    struct semantic_state **, bool *);
int context_state_import(struct function_info *, const struct semantic_state *,
    struct semantic_state **, bool *);
/*
 * A NULL visibility mapper preserves caller visibility unchanged.  A
 * non-NULL mapper translates the callee's exact exit visibility set.
 */
int context_state_map_exit(struct function_info *,
    const struct semantic_state *, const struct semantic_state *,
    const struct semantic_state *, context_lock_filter_f, void *,
    context_visibility_map_f, void *, struct semantic_state **, bool *);
/*
 * Lock identities must be canonical and stable for the function collection's
 * lifetime.  Zero modes removes the lock.
 */
int context_state_set_lock(struct function_info *,
    const struct semantic_state *, const struct lock_identity *, unsigned int,
    struct semantic_state **, bool *);
int context_state_set_visibility(struct function_info *,
    const struct semantic_state *, struct visibility_region,
    enum semantic_visibility, struct semantic_state **, bool *);
int context_state_set_competition(struct function_info *,
    const struct semantic_state *, struct competition_interval,
    struct semantic_state **, bool *);
int context_state_adjust_competition(struct function_info *,
    const struct semantic_state *, int, struct semantic_state **, bool *);
int context_competition_adjust(struct competition_interval, int,
    struct competition_interval *);
bool context_state_competition_contains(const struct semantic_state *,
    const struct semantic_state *);
int context_state_merge_competition(struct function_info *,
    const struct semantic_state *, const struct semantic_state *, bool,
    struct semantic_state **, bool *);
int context_create(struct function_info *,
    const struct binding_environment *, const struct semantic_state *,
    struct function_context **, bool *);
int context_root_create(struct function_info *,
    const struct binding_environment *, const struct semantic_state *,
    struct function_context **, bool *);
int context_effect_create(struct function_info *,
    const struct binding_environment *, const struct semantic_state *,
    struct function_context **, bool *);
int context_effect_contract_create(struct function_info *,
    const struct binding_environment *, const struct semantic_state *,
    struct function_context **, bool *);
int context_point_state_record(struct function_context *, struct analysis_point,
    const struct semantic_state *, struct point_state **, bool *);
int context_point_state_record_widened(struct function_context *,
    struct analysis_point, const struct semantic_state *, struct point_state **,
    bool *, bool *);

size_t context_count(struct function_info *);
size_t context_lock_set_count(struct function_info *);
size_t context_visibility_set_count(struct function_info *);
size_t context_visibility_sets_created(struct function_info *);
size_t context_visibility_sets_reused(struct function_info *);
size_t context_state_count(struct function_info *);
size_t context_state_lock_count(const struct semantic_state *);
unsigned int context_state_lock_modes(const struct semantic_state *,
    const struct lock_identity *);
size_t context_state_visibility_count(const struct semantic_state *);
bool context_state_visibility(const struct semantic_state *,
    struct visibility_region, enum semantic_visibility *);
bool context_state_effective_visibility(const struct semantic_state *,
    struct visibility_region, enum semantic_visibility *);
struct competition_interval context_state_competition(
    const struct semantic_state *);
size_t context_point_state_count(struct function_context *);

#endif /* CONTEXT_H */
