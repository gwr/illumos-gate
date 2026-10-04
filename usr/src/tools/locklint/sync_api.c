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
 * Own and index synchronization-function descriptors.  Family replacement
 * builds and validates a complete candidate set before changing the API.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"
#include "sync_api.h"

struct sync_api {
	avl_tree_t entries;
};

struct sync_api_entry {
	struct sync_func func;
	avl_node_t by_name;
};

static struct sync_api active_api;

#define	SYNC_FUNC(family, name, action, mode, lock_arg, mode_arg, profiles) \
	{ name, family, action, mode, lock_arg, mode_arg, profiles }
#define	WAIT_FUNC(name) \
	{ name, SYNC_FAMILY_COND_WAIT, LOCKLINT_LOCK_WAIT, \
	    LOCKLINT_MODE_MUTEX, 1, \
	    LOCKLINT_NO_ARGUMENT, LOCKLINT_API_ILLUMOS_KERNEL }

static const struct sync_func default_mutex_api[] = {
	SYNC_FUNC(SYNC_FAMILY_MUTEX, "mutex_enter", LOCKLINT_LOCK_ACQUIRE,
	    LOCKLINT_MODE_MUTEX, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_MUTEX, "mutex_exit", LOCKLINT_LOCK_RELEASE,
	    LOCKLINT_MODE_UNHELD, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_MUTEX, "mutex_lock",
	    LOCKLINT_LOCK_RESULT_ACQUIRE, LOCKLINT_MODE_MUTEX, 0,
	    LOCKLINT_NO_ARGUMENT, LOCKLINT_API_ILLUMOS_USER),
	SYNC_FUNC(SYNC_FAMILY_MUTEX, "mutex_tryenter",
	    LOCKLINT_LOCK_TRY_ACQUIRE, LOCKLINT_MODE_MUTEX, 0,
	    LOCKLINT_NO_ARGUMENT, LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_MUTEX, "mutex_trylock",
	    LOCKLINT_LOCK_TRY_ACQUIRE_ZERO, LOCKLINT_MODE_MUTEX, 0,
	    LOCKLINT_NO_ARGUMENT, LOCKLINT_API_ILLUMOS_USER),
	SYNC_FUNC(SYNC_FAMILY_MUTEX, "mutex_unlock", LOCKLINT_LOCK_RELEASE,
	    LOCKLINT_MODE_UNHELD, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_USER),
};

static const struct sync_func default_rwlock_api[] = {
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_downgrade", LOCKLINT_LOCK_DOWNGRADE,
	    LOCKLINT_MODE_READER, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_enter", LOCKLINT_LOCK_ACQUIRE,
	    LOCKLINT_MODE_UNHELD, 0, 1, LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_exit", LOCKLINT_LOCK_RELEASE,
	    LOCKLINT_MODE_UNHELD, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_rdlock", LOCKLINT_LOCK_ACQUIRE,
	    LOCKLINT_MODE_READER, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_USER),
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_tryenter",
	    LOCKLINT_LOCK_TRY_ACQUIRE, LOCKLINT_MODE_UNHELD, 0, 1,
	    LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_tryupgrade",
	    LOCKLINT_LOCK_TRY_UPGRADE, LOCKLINT_MODE_WRITER, 0,
	    LOCKLINT_NO_ARGUMENT, LOCKLINT_API_ILLUMOS_KERNEL),
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_unlock", LOCKLINT_LOCK_RELEASE,
	    LOCKLINT_MODE_UNHELD, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_USER),
	SYNC_FUNC(SYNC_FAMILY_RWLOCK, "rw_wrlock", LOCKLINT_LOCK_ACQUIRE,
	    LOCKLINT_MODE_WRITER, 0, LOCKLINT_NO_ARGUMENT,
	    LOCKLINT_API_ILLUMOS_USER),
};

static const struct sync_func default_cond_wait_api[] = {
	WAIT_FUNC("cv_reltimedwait"),
	WAIT_FUNC("cv_reltimedwait_sig"),
	WAIT_FUNC("cv_timedwait"),
	WAIT_FUNC("cv_timedwait_hires"),
	WAIT_FUNC("cv_timedwait_sig"),
	WAIT_FUNC("cv_timedwait_sig_hrtime"),
	WAIT_FUNC("cv_wait"),
	WAIT_FUNC("cv_wait_sig"),
	WAIT_FUNC("cv_wait_sig_swap"),
	WAIT_FUNC("cv_wait_sig_swap_core"),
	WAIT_FUNC("cv_wait_stop"),
	WAIT_FUNC("cv_waituntil_sig")
};

#undef WAIT_FUNC
#undef SYNC_FUNC

static int
compare_entry(const void *left_arg, const void *right_arg)
{
	const struct sync_api_entry *left = left_arg;
	const struct sync_api_entry *right = right_arg;
	int result = strcmp(left->func.name, right->func.name);

	return (AVL_ISIGN(result));
}

static void
free_entry(struct sync_api_entry *entry)
{
	free((void *)entry->func.name);
	free(entry);
}

static int
create_entry(const struct sync_func *func, struct sync_api_entry **result)
{
	struct sync_api_entry *entry;
	char *name;

	if (func == NULL || func->name == NULL || func->name[0] == '\0')
		return (EINVAL);
	entry = calloc(1, sizeof (*entry));
	if (entry == NULL)
		return (ENOMEM);
	name = malloc(strlen(func->name) + 1);
	if (name == NULL) {
		free(entry);
		return (ENOMEM);
	}
	(void) strcpy(name, func->name);
	entry->func = *func;
	entry->func.name = name;
	*result = entry;
	return (0);
}

static void
remove_entry(struct sync_api *api, struct sync_api_entry *entry)
{
	avl_remove(&api->entries, entry);
	free_entry(entry);
}

static struct sync_api_entry *
find_entry(const struct sync_api *api, const char *name, avl_index_t *where)
{
	struct sync_api_entry key = {
		.func.name = name
	};

	if (name == NULL)
		return (NULL);
	return (avl_find((avl_tree_t *)&api->entries, &key, where));
}

static int
add_entry(struct sync_api *api, const struct sync_func *func)
{
	struct sync_api_entry *entry;
	avl_index_t where;
	int error;

	error = create_entry(func, &entry);
	if (error != 0)
		return (error);
	if (avl_find(&api->entries, entry, &where) != NULL) {
		free_entry(entry);
		return (EEXIST);
	}
	avl_insert(&api->entries, entry, where);
	return (0);
}

static void
init_api(struct sync_api *api)
{
	avl_create(&api->entries, compare_entry,
	    sizeof (struct sync_api_entry),
	    offsetof(struct sync_api_entry, by_name));
}

static void
fini_api(struct sync_api *api)
{
	struct sync_api_entry *entry;

	while ((entry = avl_first(&api->entries)) != NULL)
		remove_entry(api, entry);
	avl_destroy(&api->entries);
}

static const struct sync_func *
find_func(const struct sync_api *api, const char *name)
{
	struct sync_api_entry *entry;

	entry = find_entry(api, name, NULL);
	return (entry != NULL ? &entry->func : NULL);
}

/*
 * Replace one complete family only after every candidate has been allocated
 * and checked against both the candidate set and retained families.  Failure
 * leaves the active API unchanged.
 */
static int
replace_family(struct sync_api *api, enum sync_family family,
    const struct sync_func *funcs, size_t count)
{
	struct sync_api candidates;
	struct sync_api_entry *entry;
	struct sync_api_entry *next;
	size_t index;
	int error = 0;

	if (family < 0 || family >= SYNC_FAMILY_COUNT ||
	    (count != 0 && funcs == NULL))
		return (EINVAL);
	init_api(&candidates);
	for (index = 0; index < count; index++) {
		struct sync_api_entry *existing;

		if (funcs[index].family != family) {
			error = EINVAL;
			goto out;
		}
		error = add_entry(&candidates, &funcs[index]);
		if (error != 0)
			goto out;
		existing = find_entry(api, funcs[index].name, NULL);
		if (existing != NULL && existing->func.family != family) {
			error = EEXIST;
			goto out;
		}
	}
	for (entry = avl_first(&api->entries); entry != NULL;
	    entry = next) {
		next = AVL_NEXT(&api->entries, entry);
		if (entry->func.family == family)
			remove_entry(api, entry);
	}
	while ((entry = avl_first(&candidates.entries)) != NULL) {
		avl_index_t where;

		avl_remove(&candidates.entries, entry);
		if (avl_find(&api->entries, entry, &where) != NULL)
			abort();
		avl_insert(&api->entries, entry, where);
	}
out:
	fini_api(&candidates);
	return (error);
}

static int
load_defaults(struct sync_api *api)
{
	int error;

	error = replace_family(api, SYNC_FAMILY_MUTEX, default_mutex_api,
	    sizeof (default_mutex_api) / sizeof (default_mutex_api[0]));
	if (error == 0) {
		error = replace_family(api, SYNC_FAMILY_RWLOCK,
		    default_rwlock_api,
		    sizeof (default_rwlock_api) /
		    sizeof (default_rwlock_api[0]));
	}
	if (error == 0) {
		error = replace_family(api, SYNC_FAMILY_COND_WAIT,
		    default_cond_wait_api,
		    sizeof (default_cond_wait_api) /
		    sizeof (default_cond_wait_api[0]));
	}
	return (error);
}

int
sync_api_init(void)
{
	int error;

	init_api(&active_api);
	error = load_defaults(&active_api);
	if (error != 0)
		fini_api(&active_api);
	return (error);
}

void
sync_api_fini(void)
{
	fini_api(&active_api);
}

const struct sync_func *
sync_api_find(const char *name)
{
	return (find_func(&active_api, name));
}

int
sync_api_replace(enum sync_family family, const struct sync_func *funcs,
    size_t count)
{
	return (replace_family(&active_api, family, funcs, count));
}
