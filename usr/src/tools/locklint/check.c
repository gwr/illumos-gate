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
 * Coordinate whole-program callgraph resolution and caller-context analysis.
 * Locking diagnostics will be added incrementally to the new analysis; the
 * previous factored-summary checker remains available on the locklint1 branch.
 */

#include <stdbool.h>
#include <stdio.h>

#include "analysis.h"
#include "callgraph.h"
#include "check.h"
#include "lock_identity.h"
#include "lock_order.h"

void
locklint_check_all(bool check_locks, bool show_callgraph, bool show_contexts)
{
	struct lock_identity_collection lock_identities;

	callgraph_resolve();
	lock_identity_collection_create(&lock_identities);
	if (check_locks) {
		locklint_order_build();
		locklint_order_report_declared_cycles();
	}
	if (show_callgraph)
		callgraph_dump(stdout);
	if (check_locks || show_contexts)
		analysis_run(&lock_identities, show_contexts ? stdout : NULL);
	if (check_locks) {
		locklint_order_report_observed_cycles();
		locklint_order_cleanup();
	}
	callgraph_cleanup();
	lock_identity_collection_free(&lock_identities);
}
