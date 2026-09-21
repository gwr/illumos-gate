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

#define	STATISTICS_BAR_WIDTH	40

struct statistics_counts statistics;

#define	SHOW(field) \
	(void) fprintf(stream, "statistics %s %zu\n", #field, statistics.field)

void
statistics_histogram_add(struct statistics_histogram *histogram, size_t value)
{
	size_t bucket;
	size_t upper;

	histogram->samples++;
	histogram->total += value;
	if (value > histogram->maximum)
		histogram->maximum = value;
	if (value == 0) {
		histogram->buckets[0]++;
		return;
	}
	for (bucket = 1, upper = 1;
	    bucket <= STATISTICS_HISTOGRAM_POWER_BUCKETS;
	    bucket++, upper = upper * 2 + 1) {
		if (value <= upper) {
			histogram->buckets[bucket]++;
			return;
		}
	}
	histogram->overflow++;
}

static void
statistics_histogram_show(FILE *stream, const char *name,
    const struct statistics_histogram *histogram)
{
	size_t counts[STATISTICS_HISTOGRAM_POWER_BUCKETS + 2];
	size_t first = 0;
	size_t last = 0;
	size_t largest = 0;
	size_t index;

	(void) fprintf(stream,
	    "statistics distribution %s samples %zu total %zu max %zu\n",
	    name, histogram->samples, histogram->total, histogram->maximum);
	for (index = 0; index <= STATISTICS_HISTOGRAM_POWER_BUCKETS; index++)
		counts[index] = histogram->buckets[index];
	counts[STATISTICS_HISTOGRAM_POWER_BUCKETS + 1] = histogram->overflow;
	for (index = 0; index < sizeof (counts) / sizeof (counts[0]); index++) {
		if (counts[index] == 0)
			continue;
		if (largest == 0)
			first = index;
		last = index;
		if (counts[index] > largest)
			largest = counts[index];
	}
	(void) fprintf(stream,
	    "         range |----------------------------------------| "
	    "count\n");
	for (index = first; largest != 0 && index <= last; index++) {
		char label[32];
		size_t stars;
		size_t column;

		if (index == 0) {
			(void) snprintf(label, sizeof (label), "0");
		} else if (index <= STATISTICS_HISTOGRAM_POWER_BUCKETS) {
			size_t lower = (size_t)1 << (index - 1);
			size_t upper = ((size_t)1 << index) - 1;

			if (lower == upper) {
				(void) snprintf(label, sizeof (label), "%zu", lower);
			} else {
				(void) snprintf(label, sizeof (label), "%zu-%zu",
				    lower, upper);
			}
		} else {
			(void) snprintf(label, sizeof (label), "%u+",
			    1U << STATISTICS_HISTOGRAM_POWER_BUCKETS);
		}
		stars = (size_t)((long double)counts[index] *
		    STATISTICS_BAR_WIDTH / largest);
		if (counts[index] != 0 && stars == 0)
			stars = 1;
		(void) fprintf(stream, "%14s |", label);
		for (column = 0; column < STATISTICS_BAR_WIDTH; column++)
			(void) fputc(column < stars ? '*' : ' ', stream);
		(void) fprintf(stream, "| %zu\n", counts[index]);
	}
	(void) fputc('\n', stream);
}

void
statistics_show(FILE *stream)
{
	SHOW(type_registration_symbols_visited);
	SHOW(type_registration_nodes_visited);
	SHOW(type_registry_find);
	SHOW(type_registry_duplicates);
	SHOW(type_registry_insertions);
	SHOW(type_registry_comparisons);
	SHOW(source_type_policy_refs_resolved);
	SHOW(source_type_policy_refs_retained);
	SHOW(source_type_policy_refs_deduplicated);

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

#define	SHOW_HISTOGRAM(field) \
	statistics_histogram_show(stream, #field, &statistics.field)
	SHOW_HISTOGRAM(caller_recovery_first_max_depth);
	SHOW_HISTOGRAM(caller_recovery_repeat_max_depth);
	SHOW_HISTOGRAM(caller_recovery_first_contexts_visited);
	SHOW_HISTOGRAM(caller_recovery_repeat_contexts_visited);
	SHOW_HISTOGRAM(caller_recovery_first_edges_examined);
	SHOW_HISTOGRAM(caller_recovery_repeat_edges_examined);
	SHOW_HISTOGRAM(caller_recovery_first_root_calls);
	SHOW_HISTOGRAM(caller_recovery_repeat_root_calls);
#undef SHOW_HISTOGRAM
}

#undef SHOW
