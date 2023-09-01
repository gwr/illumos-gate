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

#ifndef _TEST_DEFS_H
#define	_TEST_DEFS_H

/*
 * Describe the purpose of the file here.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define	JOB_ST_FREE	0
#define	JOB_ST_SCHED	1
#define	JOB_ST_RUN	2
#define	JOB_ST_COMPL	3

typedef struct job {
	kmutex_t	job_lock;
	kcondvar_t	job_cv;
	ushort_t	job_status;
	taskqid_t	job_tqid;
} job_t;
extern job_t *job_vec;
extern int debug;

extern ulong_t freemem;
extern ulong_t throttlefree;

extern void debug_pause(int, char *);
extern void test_taskq1(void);

extern kthread_t *lwp_kernel_create(struct proc *, void (*)(), void *,
    int, pri_t);

#ifdef __cplusplus
}
#endif

#endif /* _TEST_DEFS_H */
