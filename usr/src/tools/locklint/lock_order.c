/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Own the global lock-order graphs.  This first increment records declared
 * edges and reports cyclic declaration sets.  Acquisition and observed-edge
 * analysis will use the same canonical vertices in later increments.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "access.h"
#include "annotations.h"
#include "lock_order.h"
#include "symbol.h"

struct order_vertex;

struct observed_edge {
	struct order_vertex *before;
	struct order_vertex *after;
	struct position held_pos;
	struct position acquire_pos;
	bool has_held_pos;
	bool possible;
	struct observed_edge *next;
	struct observed_edge *next_from;
};

struct order_edge {
	struct order_vertex *before;
	struct order_vertex *after;
	struct position pos;
	struct order_edge *next;
	struct order_edge *next_from;
};

struct order_vertex {
	struct locklint_access role;
	char *name;
	unsigned int visit;
	unsigned int component;
	unsigned int observed_component;
	struct order_edge *path_edge;
	struct order_edge *edges;
	struct observed_edge *observed_edges;
	struct order_vertex *next;
};

static struct order_vertex *vertices;
static struct order_edge *edges;
static struct observed_edge *observed_edges;
static unsigned int visit_generation;
static unsigned int component_generation;

static char *
copy_string(const char *text)
{
	char *copy;
	size_t size = strlen(text) + 1;

	copy = malloc(size);
	if (copy == NULL)
		die("out of memory building lock-order graph");
	(void) memcpy(copy, text, size);
	return (copy);
}

static bool
same_ident(const struct ident *left, const struct ident *right)
{
	if (left == right)
		return (true);
	return (left != NULL && right != NULL &&
	    left->len == right->len &&
	    memcmp(left->name, right->name, left->len) == 0);
}

static bool
same_position(struct position left, struct position right)
{
	return (strcmp(stream_name(left.stream), stream_name(right.stream)) == 0 &&
	    left.line == right.line && left.pos == right.pos);
}

static int
compare_position(struct position left, struct position right)
{
	int result;

	result = strcmp(stream_name(left.stream), stream_name(right.stream));
	if (result != 0)
		return (result);
	if (left.line != right.line)
		return (left.line < right.line ? -1 : 1);
	if (left.pos != right.pos)
		return (left.pos < right.pos ? -1 : 1);
	return (0);
}

static bool
same_type_role(const struct locklint_access *left,
    const struct locklint_access *right)
{
	if (left->type == NULL || right->type == NULL)
		return (false);
	if (!same_position(left->type->pos, right->type->pos))
		return (false);
	if (left->offset != right->offset)
		return (false);
	return (same_ident(left->member != NULL ? left->member->ident : NULL,
	    right->member != NULL ? right->member->ident : NULL));
}

static bool
same_role(const struct locklint_access *left,
    const struct locklint_access *right)
{
	if ((left->root == NULL) != (right->root == NULL))
		return (false);
	if (left->root == NULL)
		return (same_type_role(left, right));
	return (locklint_same_access(left, right));
}

static struct order_vertex *
find_vertex(const struct locklint_access *role)
{
	struct order_vertex *vertex;

	for (vertex = vertices; vertex != NULL; vertex = vertex->next) {
		if (same_role(&vertex->role, role))
			return (vertex);
	}
	return (NULL);
}

static struct order_vertex *
add_vertex(const struct locklint_access *role, const char *name)
{
	struct order_vertex *vertex;

	vertex = find_vertex(role);
	if (vertex != NULL)
		return (vertex);
	vertex = calloc(1, sizeof (*vertex));
	if (vertex == NULL)
		die("out of memory building lock-order graph");
	vertex->role = *role;
	vertex->name = copy_string(name);
	vertex->next = vertices;
	vertices = vertex;
	return (vertex);
}

static char *
join_role_name(const char *base, const char *member, const char *separator)
{
	char *name;
	size_t base_len = base != NULL ? strlen(base) : 0;
	size_t member_len = member != NULL ? strlen(member) : 0;
	size_t separator_len = base_len != 0 && member_len != 0 ?
	    strlen(separator) : 0;

	name = malloc(base_len + separator_len + member_len + 1);
	if (name == NULL)
		die("out of memory naming lock-order role");
	if (base_len != 0)
		(void) memcpy(name, base, base_len);
	if (separator_len != 0)
		(void) memcpy(name + base_len, separator, separator_len);
	if (member_len != 0) {
		(void) memcpy(name + base_len + separator_len, member,
		    member_len);
	}
	name[base_len + separator_len + member_len] = '\0';
	return (name);
}

static char *
observed_role(const struct locklint_access *access,
    struct locklint_access *role)
{
	const char *base;
	const char *member;
	const char *separator;

	*role = *access;
	role->expr = NULL;
	role->path = NULL;
	role->address_base = NULL;
	role->address_offset = 0;
	member = access->member != NULL && access->member->ident != NULL ?
	    show_ident(access->member->ident) : NULL;
	if (access->object == NULL && access->member != NULL &&
	    access->type != NULL) {
		role->root = NULL;
		base = access->type->ident != NULL ?
		    show_ident(access->type->ident) : NULL;
		separator = "::";
	} else {
		base = access->root != NULL && access->root->ident != NULL ?
		    show_ident(access->root->ident) : NULL;
		separator = ".";
	}
	if (base == NULL && member == NULL)
		return (copy_string("<unknown>"));
	return (join_role_name(base, member, separator));
}

static void
add_declared_edge(const struct locklint_access *before,
    const char *before_name, const struct locklint_access *after,
    const char *after_name, const struct position *pos, void *data)
{
	struct order_vertex *from;
	struct order_vertex *to;
	struct order_edge *edge;

	(void) data;
	from = add_vertex(before, before_name);
	to = add_vertex(after, after_name);
	for (edge = from->edges; edge != NULL; edge = edge->next_from) {
		if (edge->after != to)
			continue;
		if (compare_position(*pos, edge->pos) < 0)
			edge->pos = *pos;
		return;
	}
	edge = calloc(1, sizeof (*edge));
	if (edge == NULL)
		die("out of memory building lock-order graph");
	edge->before = from;
	edge->after = to;
	edge->pos = *pos;
	edge->next_from = from->edges;
	from->edges = edge;
	edge->next = edges;
	edges = edge;
}

void
locklint_order_build(void)
{
	if (vertices != NULL || edges != NULL)
		die("lock-order graph built more than once");
	locklint_for_each_order_edge(add_declared_edge, NULL);
}

static bool
reachable(struct order_vertex *from, const struct order_vertex *target)
{
	struct order_edge *edge;

	if (from == target)
		return (true);
	from->visit = visit_generation;
	for (edge = from->edges; edge != NULL; edge = edge->next_from) {
		if (edge->after->visit == visit_generation)
			continue;
		if (reachable(edge->after, target))
			return (true);
	}
	return (false);
}

static bool
path_exists(struct order_vertex *from, struct order_vertex *to)
{
	if (++visit_generation == 0) {
		struct order_vertex *vertex;

		for (vertex = vertices; vertex != NULL; vertex = vertex->next)
			vertex->visit = 0;
		visit_generation = 1;
	}
	return (reachable(from, to));
}

static bool
vertex_is_cyclic(struct order_vertex *vertex)
{
	struct order_edge *edge;

	for (edge = vertex->edges; edge != NULL; edge = edge->next_from) {
		if (path_exists(edge->after, vertex))
			return (true);
	}
	return (false);
}

static bool
role_matches_access(const struct order_vertex *vertex,
    const struct locklint_access *access)
{
	unsigned long base;

	if (vertex->role.root != NULL)
		return (locklint_same_access(&vertex->role, access));
	if (!same_ident(vertex->role.member != NULL ?
	    vertex->role.member->ident : NULL,
	    access->member != NULL ? access->member->ident : NULL))
		return (false);
	return (locklint_access_base(access, vertex->role.type,
	    vertex->role.offset, &base));
}

static bool
find_path(struct order_vertex *from, struct order_vertex *to)
{
	struct order_edge *edge;

	from->visit = visit_generation;
	if (from == to)
		return (true);
	for (edge = from->edges; edge != NULL; edge = edge->next_from) {
		if (edge->after->visit == visit_generation)
			continue;
		edge->after->path_edge = edge;
		if (find_path(edge->after, to))
			return (true);
	}
	return (false);
}

static bool
declared_path(struct order_vertex *from, struct order_vertex *to)
{
	struct order_vertex *vertex;

	if (from == to)
		return (false);
	if (++visit_generation == 0) {
		for (vertex = vertices; vertex != NULL; vertex = vertex->next)
			vertex->visit = 0;
		visit_generation = 1;
	}
	from->path_edge = NULL;
	return (find_path(from, to));
}

static bool
path_uses_declared_cycle(struct order_vertex *from, struct order_vertex *to)
{
	struct order_vertex *vertex;

	for (vertex = to; vertex != from;
	    vertex = vertex->path_edge->before) {
		if (vertex->component != 0)
			return (true);
	}
	return (from->component != 0);
}

static void
report_declared_path(struct order_vertex *from, struct order_vertex *to)
{
	struct order_edge *edge = to->path_edge;

	if (edge->before != from)
		report_declared_path(from, edge->before);
	info(edge->pos, "locklint: declared order requires '%s' before '%s'",
	    edge->before->name, edge->after->name);
}

bool
locklint_order_check_declared(const struct locklint_access *acquired,
    const struct locklint_access *held, const struct position *pos,
    bool possible)
{
	struct order_vertex *before;

	for (before = vertices; before != NULL; before = before->next) {
		struct order_vertex *after;

		if (!role_matches_access(before, acquired))
			continue;
		for (after = vertices; after != NULL; after = after->next) {
			if (!role_matches_access(after, held) ||
			    !declared_path(before, after) ||
			    path_uses_declared_cycle(before, after))
				continue;
			if (possible) {
				warning(*pos, "locklint: lock '%s' may be acquired "
				    "out of declared order while holding '%s'",
				    before->name, after->name);
			} else {
				warning(*pos, "locklint: lock '%s' acquired out "
				    "of declared order while holding '%s'",
				    before->name, after->name);
			}
			report_declared_path(before, after);
			return (true);
		}
	}
	return (false);
}

void
locklint_order_record_observed(const struct locklint_access *held,
    const struct locklint_access *acquired, const struct position *held_pos,
    const struct position *acquire_pos, bool possible, bool explained)
{
	struct locklint_access held_role;
	struct locklint_access acquired_role;
	struct order_vertex *before;
	struct order_vertex *after;
	struct observed_edge *edge;
	char *held_name;
	char *acquired_name;

	if (explained)
		return;
	held_name = observed_role(held, &held_role);
	acquired_name = observed_role(acquired, &acquired_role);
	before = add_vertex(&held_role, held_name);
	after = add_vertex(&acquired_role, acquired_name);
	free(held_name);
	free(acquired_name);
	for (edge = before->observed_edges; edge != NULL;
	    edge = edge->next_from) {
		if (edge->after != after)
			continue;
		edge->possible &= possible;
		if (compare_position(*acquire_pos, edge->acquire_pos) < 0) {
			edge->acquire_pos = *acquire_pos;
			edge->has_held_pos = held_pos != NULL;
			if (held_pos != NULL)
				edge->held_pos = *held_pos;
		}
		return;
	}
	edge = calloc(1, sizeof (*edge));
	if (edge == NULL)
		die("out of memory recording observed lock order");
	edge->before = before;
	edge->after = after;
	edge->acquire_pos = *acquire_pos;
	edge->possible = possible;
	edge->has_held_pos = held_pos != NULL;
	if (held_pos != NULL)
		edge->held_pos = *held_pos;
	edge->next_from = before->observed_edges;
	before->observed_edges = edge;
	edge->next = observed_edges;
	observed_edges = edge;
}

static int
compare_edge_position(const void *left, const void *right)
{
	const struct order_edge *const *left_edge = left;
	const struct order_edge *const *right_edge = right;

	return (compare_position((*left_edge)->pos, (*right_edge)->pos));
}

static void
report_component(unsigned int component)
{
	struct order_edge **component_edges;
	struct order_edge *edge;
	size_t count = 0;
	size_t index = 0;

	for (edge = edges; edge != NULL; edge = edge->next) {
		if (edge->before->component == component &&
		    edge->after->component == component)
			count++;
	}
	component_edges = calloc(count, sizeof (*component_edges));
	if (component_edges == NULL)
		die("out of memory reporting lock-order cycle");
	for (edge = edges; edge != NULL; edge = edge->next) {
		if (edge->before->component == component &&
		    edge->after->component == component)
			component_edges[index++] = edge;
	}
	qsort(component_edges, count, sizeof (*component_edges),
	    compare_edge_position);
	warning(component_edges[0]->pos,
	    "locklint: declared lock order contains a cycle");
	for (index = 0; index < count; index++) {
		edge = component_edges[index];
		info(edge->pos, "locklint: '%s' must precede '%s'",
		    edge->before->name, edge->after->name);
	}
	free(component_edges);
}

void
locklint_order_report_declared_cycles(void)
{
	struct order_vertex *vertex;

	for (vertex = vertices; vertex != NULL; vertex = vertex->next) {
		struct order_vertex *candidate;

		if (vertex->component != 0 || !vertex_is_cyclic(vertex))
			continue;
		component_generation++;
		for (candidate = vertices; candidate != NULL;
		    candidate = candidate->next) {
			if (candidate->component == 0 &&
			    path_exists(vertex, candidate) &&
			    path_exists(candidate, vertex))
				candidate->component = component_generation;
		}
		report_component(component_generation);
	}
}

static bool
observed_reachable(struct order_vertex *from,
    const struct order_vertex *target)
{
	struct observed_edge *edge;

	if (from == target)
		return (true);
	from->visit = visit_generation;
	for (edge = from->observed_edges; edge != NULL;
	    edge = edge->next_from) {
		if (edge->after->visit == visit_generation)
			continue;
		if (observed_reachable(edge->after, target))
			return (true);
	}
	return (false);
}

static bool
observed_path_exists(struct order_vertex *from, struct order_vertex *to)
{
	struct order_vertex *vertex;

	if (++visit_generation == 0) {
		for (vertex = vertices; vertex != NULL; vertex = vertex->next)
			vertex->visit = 0;
		visit_generation = 1;
	}
	return (observed_reachable(from, to));
}

static bool
observed_vertex_is_cyclic(struct order_vertex *vertex)
{
	struct observed_edge *edge;

	for (edge = vertex->observed_edges; edge != NULL;
	    edge = edge->next_from) {
		if (observed_path_exists(edge->after, vertex))
			return (true);
	}
	return (false);
}

static int
compare_observed_edge_position(const void *left, const void *right)
{
	const struct observed_edge *const *left_edge = left;
	const struct observed_edge *const *right_edge = right;

	return (compare_position((*left_edge)->acquire_pos,
	    (*right_edge)->acquire_pos));
}

static void
report_observed_component(unsigned int component)
{
	struct observed_edge **component_edges;
	struct observed_edge *edge;
	size_t count = 0;
	size_t index = 0;

	for (edge = observed_edges; edge != NULL; edge = edge->next) {
		if (edge->before->observed_component == component &&
		    edge->after->observed_component == component)
			count++;
	}
	component_edges = calloc(count, sizeof (*component_edges));
	if (component_edges == NULL)
		die("out of memory reporting observed lock-order cycle");
	for (edge = observed_edges; edge != NULL; edge = edge->next) {
		if (edge->before->observed_component == component &&
		    edge->after->observed_component == component)
			component_edges[index++] = edge;
	}
	qsort(component_edges, count, sizeof (*component_edges),
	    compare_observed_edge_position);
	warning(component_edges[0]->acquire_pos,
	    "locklint: observed lock acquisitions form a potential deadlock");
	for (index = 0; index < count; index++) {
		edge = component_edges[index];
		if (edge->has_held_pos) {
			info(edge->held_pos, "locklint: lock '%s' acquired here",
			    edge->before->name);
		}
		info(edge->acquire_pos, "locklint: lock '%s' %sacquired while "
		    "holding '%s'", edge->after->name,
		    edge->possible ? "may be " : "",
		    edge->before->name);
	}
	free(component_edges);
}

void
locklint_order_report_observed_cycles(void)
{
	struct order_vertex *vertex;

	for (vertex = vertices; vertex != NULL; vertex = vertex->next) {
		struct order_vertex *candidate;

		if (vertex->observed_component != 0 ||
		    !observed_vertex_is_cyclic(vertex))
			continue;
		component_generation++;
		for (candidate = vertices; candidate != NULL;
		    candidate = candidate->next) {
			if (candidate->observed_component == 0 &&
			    observed_path_exists(vertex, candidate) &&
			    observed_path_exists(candidate, vertex)) {
				candidate->observed_component =
				    component_generation;
			}
		}
		report_observed_component(component_generation);
	}
}

void
locklint_order_cleanup(void)
{
	while (observed_edges != NULL) {
		struct observed_edge *next = observed_edges->next;

		free(observed_edges);
		observed_edges = next;
	}
	while (edges != NULL) {
		struct order_edge *next = edges->next;

		free(edges);
		edges = next;
	}
	while (vertices != NULL) {
		struct order_vertex *next = vertices->next;

		free(vertices->name);
		free(vertices);
		vertices = next;
	}
	visit_generation = 0;
	component_generation = 0;
}
