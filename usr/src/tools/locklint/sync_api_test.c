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
 * Exercise synchronization API family loading, replacement, lookup, and
 * failure coherence.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#include "sync_api.h"

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
test_sync_api(void)
{
	const struct sync_func mutexes[] = {
		{
			.name = "mutex_enter",
			.family = SYNC_FAMILY_MUTEX,
			.action = LOCKLINT_LOCK_ACQUIRE,
			.mode = LOCKLINT_MODE_MUTEX,
			.lock_argument = 0,
			.mode_argument = LOCKLINT_NO_ARGUMENT,
			.profiles = LOCKLINT_API_ILLUMOS_KERNEL
		},
		{
			.name = "mutex_exit",
			.family = SYNC_FAMILY_MUTEX,
			.action = LOCKLINT_LOCK_RELEASE,
			.mode = LOCKLINT_MODE_UNHELD,
			.lock_argument = 0,
			.mode_argument = LOCKLINT_NO_ARGUMENT,
			.profiles = LOCKLINT_API_ILLUMOS_KERNEL
		}
	};
	const struct sync_func rwlocks[] = {
		{
			.name = "rw_enter",
			.family = SYNC_FAMILY_RWLOCK,
			.action = LOCKLINT_LOCK_ACQUIRE,
			.mode = LOCKLINT_MODE_UNHELD,
			.lock_argument = 0,
			.mode_argument = 1,
			.profiles = LOCKLINT_API_ILLUMOS_KERNEL
		}
	};
	const struct sync_func waits[] = {
		{
			.name = "cv_wait",
			.family = SYNC_FAMILY_COND_WAIT,
			.action = LOCKLINT_LOCK_WAIT,
			.mode = LOCKLINT_MODE_MUTEX,
			.lock_argument = 1,
			.mode_argument = LOCKLINT_NO_ARGUMENT,
			.profiles = LOCKLINT_API_ILLUMOS_KERNEL
		}
	};
	const struct sync_func replacement[] = {
		{
			.name = "project_lock",
			.family = SYNC_FAMILY_MUTEX,
			.action = LOCKLINT_LOCK_RESULT_ACQUIRE,
			.mode = LOCKLINT_MODE_MUTEX,
			.lock_argument = 2,
			.mode_argument = LOCKLINT_NO_ARGUMENT,
			.profiles = LOCKLINT_API_ILLUMOS_USER
		},
		{
			.name = "project_unlock",
			.family = SYNC_FAMILY_MUTEX,
			.action = LOCKLINT_LOCK_RELEASE,
			.mode = LOCKLINT_MODE_UNHELD,
			.lock_argument = 2,
			.mode_argument = LOCKLINT_NO_ARGUMENT,
			.profiles = LOCKLINT_API_ILLUMOS_USER
		}
	};
	const struct sync_func duplicate[] = {
		{
			.name = "duplicate_lock",
			.family = SYNC_FAMILY_MUTEX
		},
		{
			.name = "duplicate_lock",
			.family = SYNC_FAMILY_MUTEX
		}
	};
	const struct sync_func conflict[] = {
		{
			.name = "cv_wait",
			.family = SYNC_FAMILY_MUTEX
		}
	};
	const struct sync_func wrong_family[] = {
		{
			.name = "wrong_family",
			.family = SYNC_FAMILY_RWLOCK
		}
	};
	const struct sync_func *found;
	int error;

	error = sync_api_init();
	check(error == 0, "initialize synchronization API");
	if (error != 0)
		return;
	found = sync_api_find("mutex_enter");
	check(found != NULL, "load default mutex function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_MUTEX,
		    "retain default mutex family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "retain default mutex action");
	}
	found = sync_api_find("rw_enter");
	check(found != NULL, "load default rwlock function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_RWLOCK,
		    "retain default rwlock family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "retain default rwlock action");
	}
	found = sync_api_find("cv_wait");
	check(found != NULL, "load default condition-wait function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_COND_WAIT,
		    "retain default condition-wait family");
		check(found->action == LOCKLINT_LOCK_WAIT,
		    "retain default condition-wait action");
	}

	error = sync_api_replace(SYNC_FAMILY_MUTEX, mutexes,
	    sizeof (mutexes) / sizeof (mutexes[0]));
	check(error == 0, "load mutex family");
	error = sync_api_replace(SYNC_FAMILY_RWLOCK, rwlocks,
	    sizeof (rwlocks) / sizeof (rwlocks[0]));
	check(error == 0, "load rwlock family");
	error = sync_api_replace(SYNC_FAMILY_COND_WAIT, waits,
	    sizeof (waits) / sizeof (waits[0]));
	check(error == 0, "load condition-wait family");

	found = sync_api_find("cv_wait");
	check(found != NULL, "find condition-wait function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_COND_WAIT,
		    "retain condition-wait family");
		check(found->action == LOCKLINT_LOCK_WAIT,
		    "retain wait action");
		check(found->mode == LOCKLINT_MODE_MUTEX,
		    "retain wait mode");
		check(found->lock_argument == 1,
		    "retain wait lock argument");
		check(found->profiles == LOCKLINT_API_ILLUMOS_KERNEL,
		    "retain wait profile");
	}
	check(sync_api_find("missing") == NULL,
	    "missing function lookup");

	error = sync_api_replace(SYNC_FAMILY_MUTEX, duplicate,
	    sizeof (duplicate) / sizeof (duplicate[0]));
	check(error == EEXIST, "reject duplicate replacement names");
	found = sync_api_find("mutex_enter");
	check(found != NULL, "duplicate replacement leaves old function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_MUTEX,
		    "duplicate replacement leaves old family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "duplicate replacement leaves old descriptor");
	}
	error = sync_api_replace(SYNC_FAMILY_MUTEX, conflict,
	    sizeof (conflict) / sizeof (conflict[0]));
	check(error == EEXIST, "reject conflict with retained family");
	found = sync_api_find("mutex_enter");
	check(found != NULL, "conflicting replacement leaves old function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_MUTEX,
		    "conflicting replacement leaves old family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "conflicting replacement leaves old descriptor");
	}
	error = sync_api_replace(SYNC_FAMILY_MUTEX, wrong_family,
	    sizeof (wrong_family) / sizeof (wrong_family[0]));
	check(error == EINVAL, "reject mismatched descriptor family");
	found = sync_api_find("mutex_enter");
	check(found != NULL, "family mismatch leaves old function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_MUTEX,
		    "family mismatch leaves old family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "family mismatch leaves old descriptor");
	}
	error = sync_api_replace(SYNC_FAMILY_MUTEX, NULL, 1);
	check(error == EINVAL, "reject missing replacement descriptors");
	found = sync_api_find("mutex_enter");
	check(found != NULL, "invalid replacement leaves old function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_MUTEX,
		    "invalid replacement leaves old family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "invalid replacement leaves old descriptor");
	}

	error = sync_api_replace(SYNC_FAMILY_MUTEX, replacement,
	    sizeof (replacement) / sizeof (replacement[0]));
	check(error == 0, "replace complete mutex family");
	check(sync_api_find("mutex_enter") == NULL &&
	    sync_api_find("mutex_exit") == NULL,
	    "replacement removes old family");
	found = sync_api_find("project_lock");
	check(found != NULL, "find replacement function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_MUTEX,
		    "replacement retains family");
		check(found->action == LOCKLINT_LOCK_RESULT_ACQUIRE,
		    "replacement retains action");
		check(found->mode == LOCKLINT_MODE_MUTEX,
		    "replacement retains mode");
		check(found->lock_argument == 2,
		    "replacement retains lock argument");
		check(found->profiles == LOCKLINT_API_ILLUMOS_USER,
		    "replacement retains profile");
	}
	found = sync_api_find("rw_enter");
	check(found != NULL, "replacement preserves rwlock function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_RWLOCK,
		    "replacement preserves rwlock family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "replacement preserves rwlock descriptor");
	}
	found = sync_api_find("cv_wait");
	check(found != NULL, "replacement preserves condition-wait function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_COND_WAIT,
		    "replacement preserves condition-wait family");
		check(found->action == LOCKLINT_LOCK_WAIT,
		    "replacement preserves condition-wait descriptor");
	}

	error = sync_api_replace(SYNC_FAMILY_COND_WAIT, NULL, 0);
	check(error == 0, "remove condition-wait family");
	check(sync_api_find("cv_wait") == NULL,
	    "empty replacement removes selected family");
	found = sync_api_find("project_lock");
	check(found != NULL, "empty replacement preserves mutex function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_MUTEX,
		    "empty replacement preserves mutex family");
		check(found->action == LOCKLINT_LOCK_RESULT_ACQUIRE,
		    "empty replacement preserves mutex descriptor");
	}
	found = sync_api_find("rw_enter");
	check(found != NULL, "empty replacement preserves rwlock function");
	if (found != NULL) {
		check(found->family == SYNC_FAMILY_RWLOCK,
		    "empty replacement preserves rwlock family");
		check(found->action == LOCKLINT_LOCK_ACQUIRE,
		    "empty replacement preserves rwlock descriptor");
	}
	sync_api_fini();
}

int
main(void)
{
	test_sync_api();
	if (failures != 0) {
		(void) fprintf(stderr, "%u synchronization API test(s) failed\n",
		    failures);
		return (1);
	}
	return (0);
}
