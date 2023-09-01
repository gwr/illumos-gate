/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 1999, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2023 RackTop Systems, Inc.
 */

// #include <mdb/mdb_param.h>
#include <mdb/mdb_modapi.h>
// #include <mdb/mdb_ks.h>
#include <mdb/mdb_ctf.h>

#include <sys/types.h>

#include "avl.h"
#include "list.h"
#include "taskq.h"
#include "thread.h"

/*
 * Various dcmds and walkers borrowed from the genunix mdb module.
 */

static const mdb_dcmd_t dcmds[] = {

	/* from taskq.c */
	{ "taskq", ":[-atT] [-m min_maxq] [-n name]",
	    "display a taskq", taskq, taskq_help },
	{ "taskq_entry", ":", "display a taskq_ent_t", taskq_ent },

	{ NULL }
};

static const mdb_walker_t walkers[] = {

	/* from avl.c */
	{ AVL_WALK_NAME, AVL_WALK_DESC,
		avl_walk_init, avl_walk_step, avl_walk_fini },

	/* from list.c */
	{ LIST_WALK_NAME, LIST_WALK_DESC,
		list_walk_init, list_walk_step, list_walk_fini },

	/* from taskq.c */
#if 0 // XXX: doesn't work for _USER
	{ "taskq_thread", "given a taskq_t, list all of its threads",
		taskq_thread_walk_init,
		taskq_thread_walk_step,
		taskq_thread_walk_fini },
#endif // XXX

	{ "taskq_entry", "given a taskq_t*, list all taskq_ent_t in the list",
		taskq_ent_walk_init, taskq_ent_walk_step, NULL },

	{ NULL }
};

static const mdb_modinfo_t modinfo = { MDB_API_VERSION, dcmds, walkers };

const mdb_modinfo_t *
_mdb_init(void)
{
	return (&modinfo);
}

void
_mdb_fini(void)
{
}
