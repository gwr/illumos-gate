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
 * Maintain the caller-context analysis worklist.  Queue membership is stored
 * in point-state records so enqueue does not require a search.
 */

#include <stdbool.h>
#include <stddef.h>

#include "context.h"
#include "worklist.h"

void
worklist_create(struct worklist *worklist)
{
	STAILQ_INIT(&worklist->point_states);
	worklist->length = 0;
	worklist->peak_length = 0;
}

/*
 * Add work in FIFO order unless this point state is already pending.
 */
bool
worklist_point_state_enqueue(struct worklist *worklist,
    struct point_state *point_state)
{
	if (point_state->queued)
		return (false);
	point_state->queued = true;
	STAILQ_INSERT_TAIL(&worklist->point_states, point_state, work_link);
	worklist->length++;
	if (worklist->length > worklist->peak_length)
		worklist->peak_length = worklist->length;
	return (true);
}

/*
 * Remove the oldest pending point state.  Removal makes it eligible to be
 * queued again if a dependency later publishes new information.
 */
struct point_state *
worklist_point_state_dequeue(struct worklist *worklist)
{
	struct point_state *point_state;

	point_state = STAILQ_FIRST(&worklist->point_states);
	if (point_state == NULL)
		return (NULL);
	STAILQ_REMOVE_HEAD(&worklist->point_states, work_link);
	worklist->length--;
	point_state->queued = false;
	return (point_state);
}
