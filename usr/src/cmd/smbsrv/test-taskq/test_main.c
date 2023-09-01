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

/*
 * Test program for experimenting with kernel taskqs
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <sys/taskq_impl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "test_defs.h"

extern int max_ncpus;
extern int taskq_maxbuckets;
extern int taskq_thread_timeout;
extern int taskq_thread_bucket_wait;

typedef struct cmdinfo {
	char *ci_name;
	char *ci_help;
	int (*ci_func)(int, char **);
	uint32_t flags;
} cmdinfo_t;
static cmdinfo_t cmdtable[];

char *arg0;
char cmdbuf[100];

int debug = 0;
int nthreads = 64;
int minalloc = 1024;
int maxalloc = 65536;
int tqflags = TASKQ_DYNAMIC;

taskq_t *test_tq;
job_t *job_vec;

#define	NSEM 10
ksema_t pause_sem[NSEM];
int pause_flag[NSEM];

void
debug_pause(int idx, char *msg)
{
	if (idx < 0 || idx >= NSEM)
		abort();

	if (pause_flag[idx] == 0)
		return;

	printf("debug_pause[%d]: %s\n", idx, msg);
	sema_p(&pause_sem[idx]);
}

/*
 * Just block until "compl" command sets JOB_ST_COMPL
 */
void
run_job(void *arg)
{
	job_t *job = arg;
	mutex_enter(&job->job_lock);
	job->job_status = JOB_ST_RUN;
	while (job->job_status == JOB_ST_RUN)
		cv_wait(&job->job_cv, &job->job_lock);
	job->job_status = JOB_ST_FREE;
	job->job_tqid = TASKQID_INVALID;
	mutex_exit(&job->job_lock);
}

static int
do_init(int argc, char **argv)
{
	int i;

	max_ncpus = 16;
	taskq_maxbuckets = 16;
	taskq_thread_timeout = 60; // sec.
	taskq_thread_bucket_wait = 10000; // mSec.

	taskq_init();
	test_tq = taskq_create("test", nthreads, 50,
	    minalloc, maxalloc, tqflags);

	job_vec = calloc(maxalloc, sizeof (job_t));

	for (i = 0; i < NSEM; i++) {
		sema_init(&pause_sem[i], 0, "pause", SEMA_DEFAULT, NULL);
	}

	return (0);
}

void
do_fini(void)
{
	free(job_vec);
	taskq_destroy(test_tq);
}


/*
 * Command handlers
 */

static int
do_help(int argc, char **argv)
{
	cmdinfo_t *ci;

	printf("Commands\n");
	for (ci = cmdtable; ci->ci_name != NULL; ci++) {
		printf("%s\t%s\n", ci->ci_name, ci->ci_help);
	}
	return (0);
}

/*
 * Dispatch some jobs (one, or optional job count)
 * Continue job IDs through maxalloc
 */
static int
do_disp(int argc, char **argv)
{
	job_t *job;
	taskqid_t tqid;
	int j;
	int cnt = 1;
	static int last = 0;

	if (argc > 1)
		cnt = atoi(argv[1]);

	/* Find free job slots and dispatch */
	j = last + 1;
	if (j == maxalloc)
		j = 0;
	while (cnt > 0 && j != last) {
		job = &job_vec[j];
		if (job->job_status == JOB_ST_FREE) {
			job->job_status = JOB_ST_SCHED;
			tqid = taskq_dispatch(test_tq, run_job, job, 0);
			if (tqid == TASKQID_INVALID) {
				fprintf(stderr, "taskq_dispatch failed\n");
				job->job_status = JOB_ST_FREE;
				last = j;
				return (1);
			}
			job->job_tqid = tqid;
			cnt--;
		}
		if (++j == maxalloc)
			j = 0;
	}
	last = j;

	return (0);
}

/*
 * Complete some taskq jobs:
 * compl	Complete the first running job found
 * compl j	Complete slot j
 * compl j k	Complete jobs betweeen j k
 */
static int
do_compl(int argc, char **argv)
{
	job_t *job;
	int i, j = -1, k = -1;

	if (argc == 1) {
		/* Find a running job */
		for (i = 0; i < maxalloc; i++) {
			if (job_vec[j].job_status == JOB_ST_RUN) {
				j = i;
				k = j + 1;
				break;
			}
		}
	}
	if (argc > 1) {
		j = atoi(argv[1]);
		k = j + 1;
	}
	if (argc > 2) {
		k = atoi(argv[2]);
		if (k > maxalloc) {
			k = maxalloc;
		}
	}
	if (j == -1 || k == -1) {
		fprintf(stderr, "no job to complete\n");
		return (1);
	}

	/* Complete running jobs in the range */
	for (i = j; i < k; i++) {
		job = &job_vec[i];

		if (job->job_status != JOB_ST_RUN)
			continue;
		mutex_enter(&job->job_lock);
		if (job->job_status == JOB_ST_RUN) {
			job->job_status = JOB_ST_COMPL;
			cv_signal(&job->job_cv);
		}
		mutex_exit(&job->job_lock);
	}
	return (0);
}

static void
show_jobs_st(int st, char *stname, int bidx)
{
	printf("Jobs in state %s:\n", stname);
	taskq_bucket_t *b, *idleb;
	taskq_ent_t *tqe;
	int i, bi, pi, pbi, pst;

	idleb = &test_tq->tq_buckets[test_tq->tq_nbuckets];
	pi = maxalloc;
	pbi = -2;
	pst = -1;

	for (i = 0; i < maxalloc; i++) {
		bi = -1;

		/* Filter by state */
		if (job_vec[i].job_status != st)
			goto next;

		/* Figure out the job's bucket index */
		tqe = (taskq_ent_t *) (job_vec[i].job_tqid);
		if (tqe != NULL) {
			b = tqe->tqent_un.tqent_bucket;
			if (b >= test_tq->tq_buckets && b <= idleb)
				bi = b - test_tq->tq_buckets;
		}

		/* Filter by bucket index */
		if (bidx != -1 && bidx != bi)
			goto next;

		/* Drop repeated lines */
		if ((pi + 1) != i || pbi != bi || pst != st)
			printf("  job[%d] bidx=%d\n", i, bi);

	next:
		pi = i;
		pbi = bi;
		pst = job_vec[i].job_status;
	}
}

/*
 * Show summary of jobs
 */
static int
do_show_jobs(int argc, char **argv)
{
	int i;
	int cnt_free = 0;
	int cnt_sched = 0;
	int cnt_run = 0;
	int cnt_compl = 0;

	for (i = 0; i < maxalloc; i++) {
		switch (job_vec[i].job_status) {
		case JOB_ST_FREE:
			cnt_free++;
			break;
		case JOB_ST_SCHED:
			cnt_sched++;
			break;
		case JOB_ST_RUN:
			cnt_run++;
			break;
		case JOB_ST_COMPL:
			cnt_compl++;
			break;
		}
	}
	printf("   free = %d\n", cnt_free);
	printf("  sched = %d\n", cnt_sched);
	printf("    run = %d\n", cnt_run);
	printf("  compl = %d\n", cnt_compl);

	if (argc > 1) {
		char *c;
		int bidx = -1;;

		if (argc > 2)
			bidx = atoi(argv[2]);

		for (c = argv[1]; *c != '\0'; c++) {
			switch (*c) {
			case 'f':
				show_jobs_st(JOB_ST_FREE, "free", bidx);
				break;
			case 's':
				show_jobs_st(JOB_ST_SCHED, "sched", bidx);
				break;
			case 'r':
				show_jobs_st(JOB_ST_RUN, "run", bidx);
				break;
			case 'c':
				show_jobs_st(JOB_ST_COMPL, "compl", bidx);
				break;
			default:
				fprintf(stderr, "usage: show-jobs [fsrc]\n");
				break;
			}
		}
	}

	return (0);
}

static int
do_show_tq(int argc, char **argv)
{
	int i;

	printf("Show: tq=0x%p\n", test_tq);

	if (test_tq->tq_flags & TASKQ_DYNAMIC) {
		printf("  cur threads = %d\n",
		       test_tq->tq_dnthreads);
	} else {
		printf("  tq_nthreads_target = %d\n",
		       test_tq->tq_nthreads_target);
		printf("  tq_nthreads_max = %d\n",
		       test_tq->tq_nthreads_max);
	}

	printf("  tq_minalloc = %d\n", test_tq->tq_minalloc);
	printf("  tq_maxalloc = %d\n", test_tq->tq_maxalloc);
	printf("  tq_nbuckets = %d\n", test_tq->tq_nbuckets);
	printf("  tq_maxsize  = %d\n", test_tq->tq_maxsize);
	printf("  tq_tasks    = %d\n", test_tq->tq_tasks);
	printf("  tq_executed = %d\n", test_tq->tq_executed);

	if (test_tq->tq_flags & TASKQ_DYNAMIC) {

		printf("dynamic buckets: idx, nalloc, nback, nfree, flags\n");
		for (i = 0; i <= test_tq->tq_nbuckets; i++) {
			taskq_bucket_t *b = &test_tq->tq_buckets[i];

			/* Unless arg, skip empty buckets */
			if (argc == 1 &&
			    i != test_tq->tq_nbuckets &&
			    b->tqbucket_nalloc == 0 &&
			    b->tqbucket_nbacklog == 0 &&
			    b->tqbucket_nfree == 0)
				continue;

			printf("%02d %02d %02d %02d 0x%x\n", i,
			       b->tqbucket_nalloc,
			       b->tqbucket_nbacklog,
			       b->tqbucket_nfree,
			       b->tqbucket_flags);
		}
	}

	return (0);
}

static int
do_show_tasks(int argc, char **argv)
{
	taskq_ent_t *tqe;
	taskq_bucket_t *b, *idleb;
	int bi;

	idleb = &test_tq->tq_buckets[test_tq->tq_nbuckets];

	printf("tq_tasks = %u\n", test_tq->tq_tasks);
	for (tqe = test_tq->tq_task.tqent_next;
	    tqe != &test_tq->tq_task;
	    tqe = tqe->tqent_next) {
		b = tqe->tqent_arg;
		bi = -1;

		if (b >= test_tq->tq_buckets && b <= idleb)
			bi = b - test_tq->tq_buckets;

		printf("  func 0x%p, arg 0x%p (bucket %d)\n",
		       tqe->tqent_func, tqe->tqent_arg, bi);
	}
	return (0);
}


static int
do_pause(int argc, char **argv)
{
	int idx, val;

	if (argc < 2) {
		fprintf(stderr, "Usage: give N (slot idx)");
		return (1);
	}
	idx = atoi(argv[1]);
	if (idx < 0 || idx >= NSEM) {
		fprintf(stderr, "invalid arg (slot idx)");
		return (1);
	}
	val = 1;
	if (argc > 2) {
		val = atoi(argv[2]);
	}

	if (strcmp(argv[0], "pause") == 0) {
		pause_flag[idx] = val;
	} else if (strcmp(argv[0], "unpause") == 0) {
		while (--val >= 0)
			sema_v(&pause_sem[idx]);
	} else {
		abort();
	}

	return (0);
}

/*
 * Just a handy place for a breakpoint
 */
static int
do_break(int argc, char **argv)
{
	return (0);
}

static cmdinfo_t
cmdtable[] = {
	{ "help", "List commands", do_help, 0},
	{ "show-jobs", "Show jobs", do_show_jobs, 0},
	{ "show-tq", "Show state [range]", do_show_tq, 0},
	{ "show-tasks", "Show tq_tasks", do_show_tasks, 0},
	{ "disp", "Dispatch jobs [cnt]", do_disp, 0},
	{ "compl", "Complete jobs [start [end]]", do_compl, 0},
	{ "pause", "continue paused N", do_pause, 0},
	{ "unpause", "continue paused N", do_pause, 0},
	{ "break", "Does nothing (breakpoint)", do_break, 0},
	{ 0 },
};

int
main(int argc, char *argv[])
{
	cmdinfo_t *ci;
	char *cmd;
	char *savep;
	char *sep = " \t\n";
	char *prompt = NULL;
	char *cargv[10];
	int cargc;
	int c, i, rc;

	arg0 = argv[0];

	if (isatty(0))
		prompt = "> ";

	while ((c = getopt(argc, argv, "dpm:x:t:")) != -1) {
		switch (c) {
		case 'd':
			debug++;
			break;
		case 'm':
			minalloc = atoi(optarg);
			break;
		case 'x':
			maxalloc = atoi(optarg);
			break;
		case 't':
			nthreads = atoi(optarg);
			break;
		case 'p':
			tqflags |= TASKQ_PREPOPULATE;
			break;
		case '?':
			fprintf(stderr, "bad opt: %c\n", c);
			return (2);
		}
	}

	if ((rc = do_init(argc, argv)) != 0)
		return (rc);

	for (;;) {
		if (prompt) {
			(void) fputs(prompt, stdout);
			fflush(stdout);
		}

		cmd = fgets(cmdbuf, sizeof (cmdbuf), stdin);
		if (cmd == NULL)
			break;
		if (cmd[0] == '#')
			continue;

		if (prompt == NULL) {
			/* Put commands in the output too. */
			(void) fputs(cmdbuf, stdout);
		}
		cmd = strtok_r(cmd, sep, &savep);
		if (cmd == NULL)
			continue;
		cargv[0] = cmd;
		cargc = 1;
		for (i = 1; i < 10; i++) {
			cargv[i] = strtok_r(NULL, sep, &savep);
			if (cargv[i] == NULL)
				break;
			cargc = i + 1;
		}

		for (ci = cmdtable; ci->ci_name != NULL; ci++)
			if (0 == strcmp(cmd, ci->ci_name))
				break;
		if (ci->ci_name == NULL) {
			fprintf(stderr, "%s: unknown -- try help\n", cmd);
			continue;
		}

		rc = ci->ci_func(cargc, cargv);
		if (rc != 0) {
			fprintf(stderr, "%s: error %d\n", cmd, rc);
		}
	}

	do_fini();

	return (0);
}
