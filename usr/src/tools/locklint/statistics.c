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
 * Hold process-global counters for caller-attributed operations on large
 * retained collections.  Each counter records one logical lookup or
 * enumeration request rather than internal AVL comparisons or steps.
 */

#include <stdio.h>

#include "statistics.h"

struct statistics_counts statistics;

#define	SHOW(field) \
	(void) fprintf(stream, "statistics %s %zu\n", #field, statistics.field)

void
statistics_show(FILE *stream)
{
	SHOW(call_binding_environments_find);
	SHOW(root_binding_environments_find);
	SHOW(effect_binding_environments_find);
	SHOW(call_contexts_find);
	SHOW(root_contexts_find);
	SHOW(effect_contexts_find);
	SHOW(cfg_point_states_find);
	SHOW(backedge_point_states_find);
	SHOW(backedge_record_point_states_find);
	SHOW(call_exit_point_states_find);
	SHOW(call_import_semantic_states_find);
	SHOW(call_exit_semantic_states_find);
	SHOW(lock_transition_semantic_states_find);
	SHOW(conditional_lock_semantic_states_find);
	SHOW(lock_assertion_semantic_states_find);
	SHOW(visibility_transition_semantic_states_find);
	SHOW(competition_transition_semantic_states_find);
	SHOW(backedge_widening_semantic_states_find);
	SHOW(root_semantic_states_find);
	SHOW(effect_semantic_states_find);
	SHOW(call_continuations_find);
	SHOW(call_provenance_edges_find);

	SHOW(declared_effect_contexts_enum);
	SHOW(lock_transition_contexts_enum);
	SHOW(declared_order_contexts_enum);
	SHOW(lock_assertion_contexts_enum);
	SHOW(competition_underflow_contexts_enum);
	SHOW(competition_effect_contexts_enum);
	SHOW(competition_assertion_contexts_enum);
	SHOW(protected_contexts_enum);
	SHOW(assumed_call_contexts_enum);
	SHOW(local_return_contexts_enum);
	SHOW(caller_return_contexts_enum);
	SHOW(measurement_contexts_enum);
	SHOW(cleanup_contexts_enum);

	SHOW(backedge_point_states_enum);
	SHOW(lock_transition_point_states_enum);
	SHOW(declared_order_point_states_enum);
	SHOW(lock_assertion_point_states_enum);
	SHOW(competition_underflow_point_states_enum);
	SHOW(competition_assertion_point_states_enum);
	SHOW(protected_scan_point_states_enum);
	SHOW(protected_policy_states_enum);
	SHOW(assumed_call_point_states_enum);
	SHOW(local_return_point_states_enum);
	SHOW(caller_return_point_states_enum);
	SHOW(measurement_point_states_enum);
	SHOW(cleanup_point_states_enum);

	SHOW(measurement_binding_environments_enum);
	SHOW(cleanup_binding_environments_enum);
	SHOW(measurement_semantic_states_enum);
	SHOW(cleanup_semantic_states_enum);
	SHOW(exit_publication_continuations_enum);
	SHOW(cleanup_continuations_enum);
	SHOW(caller_recovery_provenance_edges_enum);
	SHOW(cleanup_provenance_edges_enum);

	SHOW(caller_recovery_requests);
	SHOW(caller_recovery_unique_starts);
	SHOW(caller_recovery_contexts_visited);
	SHOW(caller_recovery_unique_contexts_visited);
	SHOW(caller_recovery_edges_examined);
	SHOW(caller_recovery_root_calls);
}

#undef SHOW
