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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2016 Igor Kozhukhov <ikozhukhov@gmail.com>.
 */

#include <libintl.h>
#include <libuutil.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/sysmacros.h>

#include <libzfs.h>
#include <libzutil.h>

#include "zpool_util.h"

/*
 * Private interface for iterating over pools specified on the command line.
 * Most consumers will call for_each_pool, but in order to support iostat, we
 * allow fined grained control through the zpool_list_t interface.
 */

typedef struct zpool_node {
	zpool_handle_t	*zn_handle;
	uu_avl_node_t	zn_avlnode;
	int		zn_mark;
} zpool_node_t;

struct zpool_list {
	boolean_t	zl_findall;
	boolean_t	zl_literal;
	uu_avl_t	*zl_avl;
	uu_avl_pool_t	*zl_pool;
	zprop_list_t	**zl_proplist;
};

/* ARGSUSED */
static int
zpool_compare(const void *larg, const void *rarg, void *unused)
{
	zpool_handle_t *l = ((zpool_node_t *)larg)->zn_handle;
	zpool_handle_t *r = ((zpool_node_t *)rarg)->zn_handle;
	const char *lname = zpool_get_name(l);
	const char *rname = zpool_get_name(r);

	return (strcmp(lname, rname));
}

/*
 * Callback function for pool_list_get().  Adds the given pool to the AVL tree
 * of known pools.
 */
static int
add_pool(zpool_handle_t *zhp, void *data)
{
	zpool_list_t *zlp = data;
	zpool_node_t *node = safe_malloc(sizeof (zpool_node_t));
	uu_avl_index_t idx;
	int error = 0;

	node->zn_handle = zhp;
	uu_avl_node_init(node, &node->zn_avlnode, zlp->zl_pool);
	if (uu_avl_find(zlp->zl_avl, node, NULL, &idx) == NULL) {
		if (zlp->zl_proplist != NULL)
			error = zpool_expand_proplist(zhp, zlp->zl_proplist);
		if (error == 0 && zlp->zl_proplist != NULL &&
		    zlp->zl_literal) {
			char buf[ZFS_MAXPROPLEN];

			for (zprop_list_t *entry = *zlp->zl_proplist;
			    entry != NULL; entry = entry->pl_next) {
				if (entry->pl_prop != ZPROP_INVAL &&
				    zpool_get_prop(zhp, entry->pl_prop, buf,
				    sizeof (buf), NULL, B_TRUE) == 0 &&
				    strlen(buf) > entry->pl_width)
					entry->pl_width = strlen(buf);
			}
		}
		if (error != 0) {
			zpool_close(zhp);
			free(node);
			return (-1);
		}
		uu_avl_insert(zlp->zl_avl, node, idx);
	} else {
		zpool_close(zhp);
		free(node);
		return (-1);
	}

	return (0);
}

/*
 * Create a list of pools based on the given arguments.  If we're given no
 * arguments, then iterate over all pools in the system and add them to the AVL
 * tree.  Otherwise, add only those pool explicitly specified on the command
 * line.
 */
zpool_list_t *
pool_list_get(int argc, char **argv, zprop_list_t **proplist,
    boolean_t literal, int *err)
{
	zpool_list_t *zlp;

	zlp = safe_malloc(sizeof (zpool_list_t));

	zlp->zl_pool = uu_avl_pool_create("zfs_pool", sizeof (zpool_node_t),
	    offsetof(zpool_node_t, zn_avlnode), zpool_compare, UU_DEFAULT);

	if (zlp->zl_pool == NULL)
		zpool_no_memory();

	if ((zlp->zl_avl = uu_avl_create(zlp->zl_pool, NULL,
	    UU_DEFAULT)) == NULL)
		zpool_no_memory();

	zlp->zl_proplist = proplist;
	zlp->zl_literal = literal;

	if (argc == 0) {
		(void) zpool_iter(g_zfs, add_pool, zlp);
		zlp->zl_findall = B_TRUE;
	} else {
		int i;

		for (i = 0; i < argc; i++) {
			zpool_handle_t *zhp;

			if ((zhp = zpool_open_canfail(g_zfs, argv[i])) !=
			    NULL) {
				if (add_pool(zhp, zlp) != 0)
					*err = B_TRUE;
			} else {
				*err = B_TRUE;
			}
		}
	}

	return (zlp);
}

/*
 * Search for any new pools, adding them to the list.  We only add pools when no
 * options were given on the command line.  Otherwise, we keep the list fixed as
 * those that were explicitly specified.
 */
void
pool_list_update(zpool_list_t *zlp)
{
	if (zlp->zl_findall)
		(void) zpool_iter(g_zfs, add_pool, zlp);
}

/*
 * Iterate over all pools in the list, executing the callback for each
 */
int
pool_list_iter(zpool_list_t *zlp, int unavail, zpool_iter_f func,
    void *data)
{
	zpool_node_t *node, *next_node;
	int ret = 0;

	for (node = uu_avl_first(zlp->zl_avl); node != NULL; node = next_node) {
		next_node = uu_avl_next(zlp->zl_avl, node);
		if (zpool_get_state(node->zn_handle) != POOL_STATE_UNAVAIL ||
		    unavail)
			ret |= func(node->zn_handle, data);
	}

	return (ret);
}

/*
 * Remove the given pool from the list.  When running iostat, we want to remove
 * those pools that no longer exist.
 */
void
pool_list_remove(zpool_list_t *zlp, zpool_handle_t *zhp)
{
	zpool_node_t search, *node;

	search.zn_handle = zhp;
	if ((node = uu_avl_find(zlp->zl_avl, &search, NULL, NULL)) != NULL) {
		uu_avl_remove(zlp->zl_avl, node);
		zpool_close(node->zn_handle);
		free(node);
	}
}

/*
 * Free all the handles associated with this list.
 */
void
pool_list_free(zpool_list_t *zlp)
{
	uu_avl_walk_t *walk;
	zpool_node_t *node;

	if ((walk = uu_avl_walk_start(zlp->zl_avl, UU_WALK_ROBUST)) == NULL) {
		(void) fprintf(stderr,
		    gettext("internal error: out of memory"));
		exit(1);
	}

	while ((node = uu_avl_walk_next(walk)) != NULL) {
		uu_avl_remove(zlp->zl_avl, node);
		zpool_close(node->zn_handle);
		free(node);
	}

	uu_avl_walk_end(walk);
	uu_avl_destroy(zlp->zl_avl);
	uu_avl_pool_destroy(zlp->zl_pool);

	free(zlp);
}

/*
 * Returns the number of elements in the pool list.
 */
int
pool_list_count(zpool_list_t *zlp)
{
	return (uu_avl_numnodes(zlp->zl_avl));
}

/*
 * High level function which iterates over all pools given on the command line,
 * using the pool_list_* interfaces.
 */
int
for_each_pool(int argc, char **argv, boolean_t unavail,
    zprop_list_t **proplist, boolean_t literal, zpool_iter_f func, void *data)
{
	zpool_list_t *list;
	int ret = 0;

	if ((list = pool_list_get(argc, argv, proplist, literal, &ret)) == NULL)
		return (1);

	if (pool_list_iter(list, unavail, func, data) != 0)
		ret = 1;

	pool_list_free(list);

	return (ret);
}

static int
for_each_vdev_cb(zpool_handle_t *zhp, nvlist_t *nv, pool_vdev_iter_f func,
    void *data)
{
	nvlist_t **child;
	uint_t c, children;
	int ret = 0;
	int i;
	char *type;

	const char *list[] = {
	    ZPOOL_CONFIG_SPARES,
	    ZPOOL_CONFIG_L2CACHE,
	    ZPOOL_CONFIG_CHILDREN
	};

	for (i = 0; i < ARRAY_SIZE(list); i++) {
		if (nvlist_lookup_nvlist_array(nv, list[i], &child,
		    &children) == 0) {
			for (c = 0; c < children; c++) {
				uint64_t ishole = 0;

				(void) nvlist_lookup_uint64(child[c],
				    ZPOOL_CONFIG_IS_HOLE, &ishole);

				if (ishole)
					continue;

				ret |= for_each_vdev_cb(zhp, child[c], func,
				    data);
			}
		}
	}

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) != 0)
		return (ret);

	/* Don't run our function on root vdevs */
	if (strcmp(type, VDEV_TYPE_ROOT) != 0) {
		ret |= func(zhp, nv, data);
	}

	return (ret);
}

/*
 * This is the equivalent of for_each_pool() for vdevs.  It iterates thorough
 * all vdevs in the pool, ignoring root vdevs and holes, calling func() on
 * each one.
 *
 * @zhp:	Zpool handle
 * @func:	Function to call on each vdev
 * @data:	Custom data to pass to the function
 */
int
for_each_vdev(zpool_handle_t *zhp, pool_vdev_iter_f func, void *data)
{
	nvlist_t *config, *nvroot = NULL;

	if ((config = zpool_get_config(zhp, NULL)) != NULL) {
		verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
		    &nvroot) == 0);
	}
	if (nvroot == NULL)
		return (0);
	return (for_each_vdev_cb(zhp, nvroot, func, data));
}

/*
 * Find the unique command-output columns and their display widths.
 */
static void
process_unique_cmd_columns(vdev_cmd_data_list_t *vcdl)
{
	char **uniq_cols = NULL;
	int *uniq_cols_width;
	int cnt = 0;

	for (int i = 0; i < vcdl->count; i++) {
		vdev_cmd_data_t *data = &vcdl->data[i];

		for (int j = 0; j < data->cols_cnt; j++) {
			int k;

			for (k = 0; k < cnt; k++) {
				if (strcmp(data->cols[j], uniq_cols[k]) == 0)
					break;
			}
			if (k == cnt) {
				char **tmp = realloc(uniq_cols,
				    sizeof (*uniq_cols) * (cnt + 1));

				if (tmp == NULL)
					zpool_no_memory();
				uniq_cols = tmp;
				uniq_cols[cnt++] = data->cols[j];
			}
		}
	}

	if (cnt == 0) {
		vcdl->uniq_cols = uniq_cols;
		return;
	}

	uniq_cols_width = safe_malloc(sizeof (*uniq_cols_width) * cnt);
	for (int i = 0; i < cnt; i++) {
		uniq_cols_width[i] = strlen(uniq_cols[i]);
		for (int j = 0; j < vcdl->count; j++) {
			vdev_cmd_data_t *data = &vcdl->data[j];

			for (int k = 0; k < data->cols_cnt; k++) {
				if (strcmp(data->cols[k], uniq_cols[i]) == 0) {
					uniq_cols_width[i] =
					    MAX(uniq_cols_width[i],
					    strlen(data->lines[k]));
				}
			}
		}
	}

	vcdl->uniq_cols = uniq_cols;
	vcdl->uniq_cols_cnt = cnt;
	vcdl->uniq_cols_width = uniq_cols_width;
}

/*
 * Save one output value.  Named output may contain several column=value
 * lines; unnamed output contributes only its first line.
 */
static int
vdev_process_cmd_output(vdev_cmd_data_t *data, char *line)
{
	char *col = NULL;
	char *val = line;
	char *coldup = NULL;
	char *valdup;
	char *equals;
	char **tmp;

	if (line == NULL)
		return (1);

	if ((equals = strchr(line, '=')) != NULL) {
		*equals = '\0';
		col = line;
		val = equals + 1;
	}

	if (col != NULL) {
		for (int i = 0; i < data->cols_cnt; i++) {
			if (strcmp(col, data->cols[i]) == 0)
				return (0);
		}
	}

	if ((valdup = strdup(val)) == NULL)
		zpool_no_memory();
	if (col != NULL && (coldup = strdup(col)) == NULL)
		zpool_no_memory();

	if (col != NULL) {
		tmp = realloc(data->cols,
		    (data->cols_cnt + 1) * sizeof (*data->cols));
		if (tmp == NULL)
			zpool_no_memory();
		data->cols = tmp;
	}

	tmp = realloc(data->lines,
	    (data->lines_cnt + 1) * sizeof (*data->lines));
	if (tmp == NULL)
		zpool_no_memory();
	data->lines = tmp;

	data->lines[data->lines_cnt++] = valdup;
	if (col != NULL)
		data->cols[data->cols_cnt++] = coldup;

	return (col == NULL);
}

static void
vdev_run_cmd(vdev_cmd_data_t *data, char *cmd)
{
	char *argv[2] = { cmd, NULL };
	char *env[4] = {
	    "PATH=/bin:/sbin:/usr/bin:/usr/sbin", NULL, NULL, NULL
	};
	char **lines = NULL;
	int lines_cnt = 0;
	int i;

	if (asprintf(&env[1], "VDEV_PATH=%s",
	    data->path != NULL ? data->path : "") == -1) {
		env[1] = NULL;
		zpool_no_memory();
	}
	if (asprintf(&env[2], "VDEV_UPATH=%s",
	    data->upath != NULL ? data->upath : "") == -1) {
		env[2] = NULL;
		zpool_no_memory();
	}

	if (libzfs_run_process_get_stdout_nopath(cmd, argv, env, &lines,
	    &lines_cnt) != 0)
		goto out;

	for (i = 0; i < lines_cnt; i++) {
		if (vdev_process_cmd_output(data, lines[i]) != 0)
			break;
	}

out:
	libzfs_free_str_array(lines, lines_cnt);
	free(env[1]);
	free(env[2]);
}

char *
zpool_get_cmd_search_path(void)
{
	const char *env;
	char *sp = NULL;

	if ((env = getenv("ZPOOL_SCRIPTS_PATH")) != NULL) {
		if ((sp = strdup(env)) == NULL)
			zpool_no_memory();
	} else if ((env = getenv("HOME")) != NULL) {
		if (asprintf(&sp, "%s/.zpool.d:%s", env,
		    ZPOOL_SCRIPTS_DIR) == -1)
			zpool_no_memory();
	} else if (asprintf(&sp, "%s", ZPOOL_SCRIPTS_DIR) == -1) {
		zpool_no_memory();
	}
	return (sp);
}

static void
vdev_run_cmds(void *cb_cmd_data)
{
	vdev_cmd_data_t *data = cb_cmd_data;
	char *cmddup, *cmd, *lasts;

	if ((cmddup = strdup(data->cmd)) == NULL)
		zpool_no_memory();

	for (cmd = strtok_r(cmddup, ",", &lasts); cmd != NULL;
	    cmd = strtok_r(NULL, ",", &lasts)) {
		char *sp, *dir, *dirlasts;
		char fullpath[MAXPATHLEN];

		if (strchr(cmd, '/') != NULL)
			continue;

		if ((sp = zpool_get_cmd_search_path()) == NULL)
			continue;

		for (dir = strtok_r(sp, ":", &dirlasts); dir != NULL;
		    dir = strtok_r(NULL, ":", &dirlasts)) {
			int len = snprintf(fullpath, sizeof (fullpath), "%s/%s",
			    dir, cmd);

			if (len < 0 || len >= sizeof (fullpath))
				continue;
			if (access(fullpath, X_OK) == 0) {
				vdev_run_cmd(data, fullpath);
				break;
			}
		}
		free(sp);
	}
	free(cmddup);
}

static int
for_each_vdev_run_cb(zpool_handle_t *zhp, nvlist_t *nv, void *cb_vcdl)
{
	vdev_cmd_data_list_t *vcdl = cb_vcdl;
	vdev_cmd_data_t *data, *tmp;
	char *path;
	boolean_t match = B_FALSE;

	if (nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) != 0)
		return (0);

	for (int i = 0; i < vcdl->count; i++) {
		if (strcmp(vcdl->data[i].path, path) == 0 &&
		    strcmp(vcdl->data[i].pool, zpool_get_name(zhp)) == 0)
			return (0);
	}

	for (int i = 0; i < vcdl->vdev_names_count; i++) {
		char *vname = zpool_vdev_name(vcdl->g_zfs, zhp, nv,
		    vcdl->cb_name_flags);

		if (strcmp(vcdl->vdev_names[i], vname) == 0)
			match = B_TRUE;
		free(vname);
		if (match)
			break;
	}
	if (!match && vcdl->vdev_names_count != 0)
		return (0);

	tmp = realloc(vcdl->data,
	    sizeof (*vcdl->data) * (vcdl->count + 1));
	if (tmp == NULL)
		zpool_no_memory();
	vcdl->data = tmp;
	data = &vcdl->data[vcdl->count];
	bzero(data, sizeof (*data));

	if ((data->pool = strdup(zpool_get_name(zhp))) == NULL)
		zpool_no_memory();
	if ((data->path = strdup(path)) == NULL)
		zpool_no_memory();
	data->upath = realpath(path, NULL);
	data->cmd = vcdl->cmd;

	vcdl->count++;
	return (0);
}

static int
all_pools_for_each_vdev_gather_cb(zpool_handle_t *zhp, void *cb_vcdl)
{
	return (for_each_vdev(zhp, for_each_vdev_run_cb, cb_vcdl));
}

static void
all_pools_for_each_vdev_run_vcdl(vdev_cmd_data_list_t *vcdl)
{
	/*
	 * Parallel execution is process job control.  A native implementation
	 * should launch and reap a bounded set of helpers rather than dedicate
	 * threads to waiting for them.
	 */
	for (int i = 0; i < vcdl->count; i++)
		vdev_run_cmds(&vcdl->data[i]);
}

vdev_cmd_data_list_t *
all_pools_for_each_vdev_run(int argc, char **argv, char *cmd,
    libzfs_handle_t *hdl, char **vdev_names, int vdev_names_count,
    int cb_name_flags)
{
	vdev_cmd_data_list_t *vcdl;

	vcdl = safe_malloc(sizeof (*vcdl));
	vcdl->cmd = cmd;
	vcdl->g_zfs = hdl;
	vcdl->vdev_names = vdev_names;
	vcdl->vdev_names_count = vdev_names_count;
	vcdl->cb_name_flags = cb_name_flags;

	(void) for_each_pool(argc, argv, B_TRUE, NULL,
	    all_pools_for_each_vdev_gather_cb, vcdl);
	all_pools_for_each_vdev_run_vcdl(vcdl);
	process_unique_cmd_columns(vcdl);

	return (vcdl);
}

void
free_vdev_cmd_data_list(vdev_cmd_data_list_t *vcdl)
{
	if (vcdl == NULL)
		return;

	free(vcdl->uniq_cols);
	free(vcdl->uniq_cols_width);

	for (int i = 0; i < vcdl->count; i++) {
		vdev_cmd_data_t *data = &vcdl->data[i];

		free(data->path);
		free(data->pool);
		free(data->upath);
		for (int j = 0; j < data->lines_cnt; j++)
			free(data->lines[j]);
		free(data->lines);
		for (int j = 0; j < data->cols_cnt; j++)
			free(data->cols[j]);
		free(data->cols);
	}
	free(vcdl->data);
	free(vcdl);
}
