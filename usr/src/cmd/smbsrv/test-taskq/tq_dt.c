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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2014 Joyent, Inc.  All rights reserved.
 */

/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright 2022 Tintri by DDN, Inc. All rights reserved.
 * Copyright 2023 RackTop Systems, Inc.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/taskq_impl.h>
#include <stdio.h>
#include "test_defs.h"
#include "tq_dt.h"

void
dtrace_taskq__enqueue(taskq_t *tq, taskq_ent_t *tqe)
{
	/* With dynamic, arg is a bucket */
	taskq_bucket_t *bucket = tqe->tqent_arg;

	if (debug) {
		if (bucket >= tq->tq_buckets &&
		    bucket < &tq->tq_buckets[tq->tq_nbuckets]) {
			/* arg is a bucket */
			int bidx = bucket - tq->tq_buckets;

			printf("dtrace_taskq__enqueue: "
			       "func = 0x%p, arg = 0x%p (bucket[%d])\n",
			       tqe->tqent_func, tqe->tqent_arg, bidx);
		} else {
			printf("dtrace_taskq__enqueue: "
			       "func = 0x%p, arg = 0x%p (not bucket?)\n",
			       tqe->tqent_func, tqe->tqent_arg);
		}
	}
}

void
dtrace_taskq__thread__start(taskq_t *tq)
{
	if (debug) {
		printf("dtrace_taskq__thread__start: nthreads = %d\n",
		       tq->tq_nthreads);
	}
}

void
dtrace_taskq__thread__end(taskq_t *tq)
{
	if (debug) {
		printf("dtrace_taskq__thread__end: nthreads = %d\n",
		       tq->tq_nthreads);
	}
}

void
dtrace_taskq__d__enqueue(taskq_bucket_t *bucket, taskq_ent_t *tqe)
{
	taskq_t *tq = bucket->tqbucket_taskq;
	job_t *job = tqe->tqent_arg;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		int jidx = job - job_vec;
		printf("dtrace_taskq__d__enqueue: "
		       "bucket[%d] job[%d] thr=0x%p\n",
		       bidx, jidx, tqe->tqent_thread);
	}
}

void
dtrace_taskq__x__backlog(taskq_bucket_t *bucket, taskq_ent_t *tqe)
{
	taskq_t *tq = bucket->tqbucket_taskq;
	job_t *job = tqe->tqent_arg;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		int jidx = job - job_vec;
		printf("dtrace_taskq__x__backlog: bucket[%d] job[%d]\n",
		       bidx, jidx);
	}
}

/* NON-dynamic taskq, used for async thread create. */
void
dtrace_taskq__exec__start(taskq_t *tq, taskq_ent_t *tqe)
{
	/* With dynamic, arg is a bucket */
	taskq_bucket_t *bucket = tqe->tqent_arg;

	if (debug > 1) {
		/* arg is a bucket */
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__exec__start: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__exec__end(taskq_t *tq, taskq_ent_t *tqe)
{
	/* With dynamic, arg is a bucket */
	taskq_bucket_t *bucket = tqe->tqent_arg;

	if (debug > 1) {
		/* arg is a bucket */
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__exec__end: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__exec__start(taskq_t *tq, taskq_bucket_t *bucket,
    taskq_ent_t *tqe)
{
	job_t *job = tqe->tqent_arg;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		int jidx = job - job_vec;
		printf("dtrace_taskq__d__exec__start: bucket[%d] job[%d]\n",
		       bidx, jidx);
	}
}

void
dtrace_taskq__d__exec__end(taskq_t *tq, taskq_bucket_t *bucket,
    taskq_ent_t *tqe)
{
	job_t *job = tqe->tqent_arg;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		int jidx = job - job_vec;
		printf("dtrace_taskq__d__exec__end: bucket[%d] job[%d]\n",
		       bidx, jidx);
	}
}

void
dtrace_taskq__d__svc__start(taskq_t *tq, taskq_bucket_t *bucket)
{
	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__thred__start: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__svc__end(taskq_t *tq, taskq_bucket_t *bucket)
{
	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__thread__end: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__thread__start(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_bucket_t *bucket = tqe->tqent_un.tqent_bucket;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__thred__start: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__thread__end(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_bucket_t *bucket = tqe->tqent_un.tqent_bucket;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__thread__end: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__idledisp(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_bucket_t *bucket = tqe->tqent_un.tqent_bucket;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__idledisp: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__wait1(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_bucket_t *bucket = tqe->tqent_un.tqent_bucket;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__wait1: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__wait2(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_bucket_t *bucket = tqe->tqent_un.tqent_bucket;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__wait2: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__migration(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_bucket_t *bucket = tqe->tqent_un.tqent_bucket;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__migration: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__d__redirect(taskq_t *tq, taskq_ent_t *tqe)
{
	taskq_bucket_t *bucket = tqe->tqent_un.tqent_bucket;

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__d__redirect: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__redist__fails(taskq_t *tq, taskq_bucket_t *bucket)
{

	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__redist__fails: bucket[%d]\n", bidx);
	}
}

void
dtrace_taskq__bucket__redist__ret(taskq_t *tq, taskq_bucket_t *bucket,
    taskq_ent_t *tqe)
{
	if (debug) {
		int bidx = bucket - tq->tq_buckets;
		printf("dtrace_taskq__bucket__redist_ret: "
		       "bucket[%d] ret=%p\n", bidx, tqe);
	}
}
