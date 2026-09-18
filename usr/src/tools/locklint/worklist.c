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
worklist_init(struct worklist *worklist)
{
	*worklist = (struct worklist){ 0 };
}

/*
 * Add work in FIFO order unless this point state is already pending.
 */
bool
worklist_enqueue(struct worklist *worklist,
    struct point_state *point_state)
{
	if (point_state->queued)
		return (false);
	point_state->queued = true;
	point_state->work_next = NULL;
	if (worklist->tail != NULL)
		worklist->tail->work_next = point_state;
	else
		worklist->head = point_state;
	worklist->tail = point_state;
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
worklist_dequeue(struct worklist *worklist)
{
	struct point_state *point_state = worklist->head;

	if (point_state == NULL)
		return (NULL);
	worklist->head = point_state->work_next;
	if (worklist->head == NULL)
		worklist->tail = NULL;
	worklist->length--;
	point_state->queued = false;
	point_state->work_next = NULL;
	return (point_state);
}
