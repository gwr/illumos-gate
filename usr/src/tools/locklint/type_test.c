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
 * Exercise cross-translation-unit type indexing with small synthetic Sparse
 * symbols.  Structural type comparison is intentionally outside this first
 * increment.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "symbol.h"
#include "token.h"
#include "type.h"

static unsigned int failures;

void
locklint_init_include_path(void)
{
}

static void
check(bool condition, const char *message)
{
	if (condition)
		return;
	(void) fprintf(stderr, "FAIL: %s\n", message);
	failures++;
}

struct type_results {
	const struct ll_type *types[4];
	size_t count;
};

static bool
collect_type(const struct ll_type *type, void *data)
{
	struct type_results *results = data;

	check(results->count < sizeof (results->types) /
	    sizeof (results->types[0]), "name lookup result capacity");
	if (results->count < sizeof (results->types) /
	    sizeof (results->types[0]))
		results->types[results->count++] = type;
	return (true);
}

static void
register_symbol(struct symbol *symbol)
{
	struct symbol_list *symbols = NULL;

	add_symbol(&symbols, symbol);
	type_symbols_register(symbols);
	free_ptr_list(&symbols);
}

static void
test_type_indexes(void)
{
	char first_header[] = "common.h";
	char second_header[] = "common.h";
	struct stream streams[] = {
		{ .name = first_header },
		{ .name = second_header },
		{ .name = "other.h" }
	};
	struct ident *tag = built_in_ident("record");
	struct ident *alias = built_in_ident("record_t");
	struct symbol first = {
		.type = SYM_STRUCT,
		.namespace = NS_STRUCT,
		.pos = { .stream = 0, .line = 10, .pos = 4 },
		.ident = tag,
		.examined = 1
	};
	struct symbol alias_symbol = {
		.type = SYM_NODE,
		.namespace = NS_TYPEDEF,
		.ident = alias,
		.ctype = { .base_type = &first }
	};
	struct symbol second = {
		.type = SYM_STRUCT,
		.namespace = NS_STRUCT,
		.pos = { .stream = 1, .line = 10, .pos = 4 },
		.ident = tag,
		.examined = 1
	};
	struct symbol other = {
		.type = SYM_STRUCT,
		.namespace = NS_STRUCT,
		.pos = { .stream = 2, .line = 10, .pos = 4 },
		.ident = tag,
		.examined = 1
	};
	const struct ll_type *first_type;
	const struct ll_type *second_type;
	const struct ll_type *other_type;
	struct type_results results = { 0 };

	input_streams = streams;
	input_stream_nr = sizeof (streams) / sizeof (streams[0]) - 1;
	type_registry_create();

	register_symbol(&first);
	register_symbol(&alias_symbol);
	register_symbol(&second);

	first_type = type_lookup_exact(&first);
	second_type = type_lookup_exact(&second);
	check(first_type != NULL, "first exact type is indexed");
	check(first_type == second_type,
	    "same source origin uses one locklint type");
	check(type_instance_count(first_type) == 2,
	    "duplicate registration does not inflate instance count");

	type_name_visit_types(tag, collect_type, &results);
	check(results.count == 1 && results.types[0] == first_type,
	    "tag name resolves once for a shared origin");
	(void) memset(&results, 0, sizeof (results));
	type_name_visit_types(alias, collect_type, &results);
	check(results.count == 1 && results.types[0] == first_type,
	    "typedef alias resolves to the same type");

	register_symbol(&other);
	other_type = type_lookup_exact(&other);
	check(other_type != NULL && other_type != first_type,
	    "different source origin uses a distinct type");
	(void) memset(&results, 0, sizeof (results));
	type_name_visit_types(tag, collect_type, &results);
	check(results.count == 2,
	    "one name can resolve to multiple source types");

	type_registry_destroy();
}

int
main(void)
{
	test_type_indexes();
	if (failures != 0) {
		(void) fprintf(stderr, "%u test failure%s\n", failures,
		    failures == 1 ? "" : "s");
		return (1);
	}
	return (0);
}
