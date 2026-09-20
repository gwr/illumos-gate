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

#ifndef STATISTICS_H
#define	STATISTICS_H

#include <stddef.h>
#include <stdio.h>

/*
 * Count logical requests to search or enumerate collections whose scale has
 * already been measured.  Names describe why locklint requested the
 * operation; the find and enum suffixes describe the operation itself.
 */
struct statistics_counts {
	size_t call_binding_environments_find;
	size_t root_binding_environments_find;
	size_t effect_binding_environments_find;
	size_t call_contexts_find;
	size_t root_contexts_find;
	size_t effect_contexts_find;
	size_t cfg_point_states_find;
	size_t backedge_point_states_find;
	size_t backedge_record_point_states_find;
	size_t call_exit_point_states_find;
	size_t call_import_semantic_states_find;
	size_t call_exit_semantic_states_find;
	size_t lock_transition_semantic_states_find;
	size_t conditional_lock_semantic_states_find;
	size_t lock_assertion_semantic_states_find;
	size_t visibility_transition_semantic_states_find;
	size_t competition_transition_semantic_states_find;
	size_t backedge_widening_semantic_states_find;
	size_t root_semantic_states_find;
	size_t effect_semantic_states_find;
	size_t call_continuations_find;
	size_t call_provenance_edges_find;

	size_t declared_effect_contexts_enum;
	size_t lock_transition_contexts_enum;
	size_t declared_order_contexts_enum;
	size_t lock_assertion_contexts_enum;
	size_t competition_underflow_contexts_enum;
	size_t competition_effect_contexts_enum;
	size_t competition_assertion_contexts_enum;
	size_t protected_contexts_enum;
	size_t assumed_call_contexts_enum;
	size_t local_return_contexts_enum;
	size_t caller_return_contexts_enum;
	size_t measurement_contexts_enum;
	size_t cleanup_contexts_enum;

	size_t backedge_point_states_enum;
	size_t lock_transition_point_states_enum;
	size_t declared_order_point_states_enum;
	size_t lock_assertion_point_states_enum;
	size_t competition_underflow_point_states_enum;
	size_t competition_assertion_point_states_enum;
	size_t protected_scan_point_states_enum;
	size_t protected_policy_states_enum;
	size_t assumed_call_point_states_enum;
	size_t local_return_point_states_enum;
	size_t caller_return_point_states_enum;
	size_t measurement_point_states_enum;
	size_t cleanup_point_states_enum;

	size_t measurement_binding_environments_enum;
	size_t cleanup_binding_environments_enum;
	size_t measurement_semantic_states_enum;
	size_t cleanup_semantic_states_enum;
	size_t exit_publication_continuations_enum;
	size_t cleanup_continuations_enum;
	size_t caller_recovery_provenance_edges_enum;
	size_t cleanup_provenance_edges_enum;
};

extern struct statistics_counts statistics;

void statistics_show(FILE *);

#endif /* STATISTICS_H */
