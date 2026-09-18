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

#ifndef WORKLIST_H
#define	WORKLIST_H

#include <stdbool.h>
#include <stddef.h>

struct point_state;

/*
 * Work is processed in FIFO order.  Point-state records may be requeued after
 * removal when a dependency publishes new information.
 */
struct worklist {
	struct point_state *head;
	struct point_state *tail;
	size_t length;
	size_t peak_length;
};

void worklist_init(struct worklist *);
bool worklist_enqueue(struct worklist *, struct point_state *);
struct point_state *worklist_dequeue(struct worklist *);

#endif /* WORKLIST_H */
