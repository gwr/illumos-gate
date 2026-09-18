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
 * Exercise FIFO scheduling, duplicate suppression, and reactivation without
 * requiring Sparse parsing or caller-context analysis.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "context.h"
#include "worklist.h"

static unsigned int failures;

static void
check(bool condition, const char *message)
{
	if (condition)
		return;
	(void) fprintf(stderr, "FAIL: %s\n", message);
	failures++;
}

static void
test_worklist(void)
{
	struct point_state first = { 0 };
	struct point_state second = { 0 };
	struct worklist worklist;

	worklist_create(&worklist);
	check(worklist_point_state_enqueue(&worklist, &first),
	    "enqueue first point state");
	check(!worklist_point_state_enqueue(&worklist, &first),
	    "suppress duplicate queued point state");
	check(worklist_point_state_enqueue(&worklist, &second),
	    "enqueue second point state");
	check(worklist.length == 2, "worklist records current length");
	check(worklist.peak_length == 2, "worklist records peak length");
	check(worklist_point_state_dequeue(&worklist) == &first,
	    "worklist preserves FIFO order");
	check(worklist_point_state_dequeue(&worklist) == &second,
	    "worklist returns second point state");
	check(worklist_point_state_dequeue(&worklist) == NULL,
	    "empty worklist returns no point state");
	check(worklist_point_state_enqueue(&worklist, &first),
	    "dequeued point state can be requeued");
	check(worklist_point_state_dequeue(&worklist) == &first,
	    "requeued point state is returned");
	check(worklist.length == 0, "worklist is empty after removal");
	check(worklist.peak_length == 2,
	    "requeue does not reduce recorded peak length");
}

int
main(void)
{
	test_worklist();
	return (failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
