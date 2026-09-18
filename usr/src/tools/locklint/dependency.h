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

#ifndef DEPENDENCY_H
#define	DEPENDENCY_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/queue.h>

#include "context.h"

struct worklist;

/*
 * Exit generations impose publication order without making generation
 * counters part of semantic state or context identity.
 */
struct context_exit {
	const struct semantic_state *state;
	unsigned int generation;
	SLIST_ENTRY(context_exit) link;
};

/*
 * A continuation records how to resume one caller from a callee context.
 * It consumes published exits in generation order.
 */
struct continuation {
	struct function_context *callee_context;
	struct function_context *caller_context;
	struct analysis_point resume_point;
	const struct semantic_state *caller_state;
	const struct binding_environment *callee_bindings;
	unsigned int last_consumed_generation;
	SLIST_ENTRY(continuation) link;
};

void dependency_records_free(struct function_context *);

int dependency_exit_publish(struct function_context *,
    const struct semantic_state *, struct context_exit **, bool *);
int dependency_continuation_create(struct function_context *,
    struct function_context *, struct analysis_point,
    const struct semantic_state *, const struct binding_environment *,
    struct continuation **, bool *);

const struct context_exit *dependency_continuation_next_exit(
    const struct continuation *);
int dependency_continuation_apply_exit(struct continuation *,
    const struct context_exit *, const struct semantic_state *,
    struct worklist *, struct point_state **, bool *);

size_t dependency_exit_count(const struct function_context *);
size_t dependency_continuation_count(const struct function_context *);

#endif /* DEPENDENCY_H */
