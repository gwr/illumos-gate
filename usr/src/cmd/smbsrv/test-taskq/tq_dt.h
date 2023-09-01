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
 * Copyright 2023 RackTop Systems, Inc.
 */

#ifndef _TQ_DT_H
#define	_TQ_DT_H

#ifdef __cplusplus
extern "C" {
#endif

void dtrace_taskq__enqueue(taskq_t *tq, taskq_ent_t *tqe);
void dtrace_taskq__thread__start(taskq_t *);
void dtrace_taskq__thread__end(taskq_t *);

void dtrace_taskq__exec__start(taskq_t *tq, taskq_ent_t *tqe);
void dtrace_taskq__exec__end(taskq_t *tq, taskq_ent_t *tqe);

void dtrace_taskq__d__enqueue(taskq_bucket_t *b, taskq_ent_t *tqe);
void dtrace_taskq__x__backlog(taskq_bucket_t *b, taskq_ent_t *tqe);

void dtrace_taskq__d__exec__start(taskq_t *, taskq_bucket_t *, taskq_ent_t *);
void dtrace_taskq__d__exec__end(taskq_t *, taskq_bucket_t *, taskq_ent_t *);

void dtrace_taskq__d__svc__start(taskq_t *, taskq_bucket_t *);
void dtrace_taskq__d__svc__end(taskq_t *, taskq_bucket_t *);

void dtrace_taskq__d__thread__start(taskq_t *, taskq_ent_t *);
void dtrace_taskq__d__thread__end(taskq_t *, taskq_ent_t *);

void dtrace_taskq__d__wait1(taskq_t *, taskq_ent_t *);
void dtrace_taskq__d__wait2(taskq_t *, taskq_ent_t *);

void dtrace_taskq__d__idledisp(taskq_t *, taskq_ent_t *);
void dtrace_taskq__d__migration(taskq_t *, taskq_ent_t *);
void dtrace_taskq__d__redirect(taskq_t *, taskq_ent_t *);

void dtrace_taskq__redist__fails(taskq_t *, taskq_bucket_t *);
void dtrace_taskq__bucket__redist__ret(taskq_t *, taskq_bucket_t *,
    taskq_ent_t *);

#ifdef __cplusplus
}
#endif

#endif	/* _TQ_DT_H */
