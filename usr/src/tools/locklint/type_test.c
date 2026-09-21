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
register_type(struct symbol *type)
{
	struct symbol declaration = {
		.type = SYM_NODE,
		.ctype = { .base_type = type }
	};

	register_symbol(&declaration);
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

static void
test_type_shapes(void)
{
	struct stream streams[] = {
		{ .name = "shared.h" },
		{ .name = "shared.h" },
		{ .name = "other.h" }
	};
	struct symbol signed_int_first = {
		.type = SYM_BASETYPE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_SIGNED,
			.alignment = 4,
			.base_type = &int_type
		}
	};
	struct symbol signed_int_second = {
		.type = SYM_BASETYPE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_SIGNED | MOD_EXPLICITLY_SIGNED,
			.alignment = 4,
			.base_type = &int_type
		}
	};
	struct symbol unsigned_int = {
		.type = SYM_BASETYPE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_UNSIGNED,
			.alignment = 4,
			.base_type = &int_type
		}
	};
	struct symbol pointer_first = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.alignment = 8,
			.base_type = &signed_int_first
		}
	};
	struct symbol pointer_second = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.alignment = 8,
			.base_type = &signed_int_second
		}
	};
	struct symbol pointer_unsigned = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.alignment = 8,
			.base_type = &unsigned_int
		}
	};
	struct symbol const_first = {
		.type = SYM_NODE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_CONST,
			.alignment = 4,
			.base_type = &signed_int_first
		}
	};
	struct symbol const_second = {
		.type = SYM_NODE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_CONST,
			.alignment = 4,
			.base_type = &signed_int_second
		}
	};
	struct symbol plain_wrapper = {
		.type = SYM_NODE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.alignment = 4,
			.base_type = &signed_int_first
		}
	};
	struct symbol array_four_first = {
		.type = SYM_ARRAY,
		.bit_size = 128,
		.examined = 1,
		.ctype = {
			.alignment = 4,
			.base_type = &signed_int_first
		}
	};
	struct symbol array_four_second = {
		.type = SYM_ARRAY,
		.bit_size = 128,
		.examined = 1,
		.ctype = {
			.alignment = 4,
			.base_type = &signed_int_second
		}
	};
	struct symbol array_eight = {
		.type = SYM_ARRAY,
		.bit_size = 256,
		.examined = 1,
		.ctype = {
			.alignment = 4,
			.base_type = &signed_int_first
		}
	};
	struct symbol aggregate_first = {
		.type = SYM_STRUCT,
		.pos = { .stream = 0, .line = 20, .pos = 2 },
		.examined = 1
	};
	struct symbol aggregate_second = {
		.type = SYM_STRUCT,
		.pos = { .stream = 1, .line = 20, .pos = 2 },
		.examined = 1
	};
	struct symbol aggregate_other = {
		.type = SYM_STRUCT,
		.pos = { .stream = 2, .line = 20, .pos = 2 },
		.examined = 1
	};
	struct symbol aggregate_pointer_first = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.alignment = 8,
			.base_type = &aggregate_first
		}
	};
	struct symbol aggregate_pointer_second = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.alignment = 8,
			.base_type = &aggregate_second
		}
	};
	struct symbol aggregate_pointer_other = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.alignment = 8,
			.base_type = &aggregate_other
		}
	};
	struct symbol argument_first = {
		.type = SYM_NODE,
		.examined = 1,
		.ctype = { .base_type = &pointer_first }
	};
	struct symbol argument_second = {
		.type = SYM_NODE,
		.examined = 1,
		.ctype = { .base_type = &pointer_second }
	};
	struct symbol function_first = {
		.type = SYM_FN,
		.examined = 1,
		.ctype = { .base_type = &signed_int_first }
	};
	struct symbol function_second = {
		.type = SYM_FN,
		.examined = 1,
		.ctype = { .base_type = &signed_int_second }
	};
	struct symbol function_variadic = {
		.type = SYM_FN,
		.variadic = 1,
		.examined = 1,
		.ctype = { .base_type = &signed_int_first }
	};

	input_streams = streams;
	input_stream_nr = sizeof (streams) / sizeof (streams[0]) - 1;
	add_symbol(&function_first.arguments, &argument_first);
	add_symbol(&function_second.arguments, &argument_second);
	add_symbol(&function_variadic.arguments, &argument_first);
	type_registry_create();
	register_type(&pointer_first);
	register_type(&pointer_second);
	register_type(&pointer_unsigned);
	register_type(&const_first);
	register_type(&const_second);
	register_type(&plain_wrapper);
	register_type(&array_four_first);
	register_type(&array_four_second);
	register_type(&array_eight);
	register_type(&aggregate_pointer_first);
	register_type(&aggregate_pointer_second);
	register_type(&aggregate_pointer_other);
	register_type(&function_first);
	register_type(&function_second);
	register_type(&function_variadic);

	check(type_lookup_exact(&signed_int_first) ==
	    type_lookup_exact(&signed_int_second),
	    "equivalent basic types are interned");
	check(type_lookup_exact(&signed_int_first) !=
	    type_lookup_exact(&unsigned_int),
	    "different basic signedness remains distinct");
	check(type_lookup_exact(&pointer_first) ==
	    type_lookup_exact(&pointer_second),
	    "equivalent pointer types are interned");
	check(type_lookup_exact(&pointer_first) !=
	    type_lookup_exact(&pointer_unsigned),
	    "pointer referent is part of its shape");
	check(type_lookup_exact(&const_first) ==
	    type_lookup_exact(&const_second),
	    "equivalent qualified types are interned");
	check(type_lookup_exact(&const_first) !=
	    type_lookup_exact(&signed_int_first),
	    "qualifiers are part of type shape");
	check(type_lookup_exact(&plain_wrapper) ==
	    type_lookup_exact(&signed_int_first),
	    "plain Sparse node wrapper is normalized away");
	check(type_lookup_exact(&array_four_first) ==
	    type_lookup_exact(&array_four_second),
	    "equivalent array types are interned");
	check(type_lookup_exact(&array_four_first) !=
	    type_lookup_exact(&array_eight),
	    "array extent is part of type shape");
	check(type_lookup_exact(&aggregate_pointer_first) ==
	    type_lookup_exact(&aggregate_pointer_second),
	    "pointer shape uses the locklint aggregate type");
	check(type_lookup_exact(&aggregate_pointer_first) !=
	    type_lookup_exact(&aggregate_pointer_other),
	    "different aggregate origins produce different pointer types");
	check(type_lookup_exact(&function_first) ==
	    type_lookup_exact(&function_second),
	    "equivalent function types are interned");
	check(type_lookup_exact(&function_first) !=
	    type_lookup_exact(&function_variadic),
	    "function variadic flag is part of its shape");

	type_registry_destroy();
	free_ptr_list(&function_first.arguments);
	free_ptr_list(&function_second.arguments);
	free_ptr_list(&function_variadic.arguments);
}

static void
test_aggregate_members(void)
{
	struct stream streams[] = {
		{ .name = "members.h" }
	};
	struct ident *tag = built_in_ident("container");
	struct symbol integer = {
		.type = SYM_BASETYPE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_SIGNED,
			.alignment = 4,
			.base_type = &int_type
		}
	};
	struct symbol unsigned_integer = {
		.type = SYM_BASETYPE,
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_UNSIGNED,
			.alignment = 4,
			.base_type = &int_type
		}
	};
	struct symbol aggregate = {
		.type = SYM_STRUCT,
		.namespace = NS_STRUCT,
		.pos = { .stream = 0, .line = 12, .pos = 1 },
		.ident = tag,
		.bit_size = 192,
		.examined = 1,
		.ctype = { .alignment = 8 }
	};
	struct symbol self_pointer = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.alignment = 8,
			.base_type = &aggregate
		}
	};
	struct symbol bitfield_type = {
		.type = SYM_BITFIELD,
		.bit_size = 3,
		.examined = 1,
		.ctype = {
			.alignment = 4,
			.base_type = &unsigned_integer
		}
	};
	struct symbol value_member = {
		.type = SYM_NODE,
		.ident = built_in_ident("value"),
		.offset = 0,
		.bit_size = 32,
		.examined = 1,
		.ctype = { .base_type = &integer }
	};
	struct symbol next_member = {
		.type = SYM_NODE,
		.ident = built_in_ident("next"),
		.offset = 8,
		.bit_size = 64,
		.examined = 1,
		.ctype = { .base_type = &self_pointer }
	};
	struct symbol flag_member = {
		.type = SYM_NODE,
		.ident = built_in_ident("flag"),
		.offset = 16,
		.bit_size = 3,
		.bit_offset = 5,
		.examined = 1,
		.ctype = { .base_type = &bitfield_type }
	};
	const struct ll_type *type;
	const struct type_member *members;

	input_streams = streams;
	input_stream_nr = 0;
	add_symbol(&aggregate.symbol_list, &value_member);
	add_symbol(&aggregate.symbol_list, &next_member);
	add_symbol(&aggregate.symbol_list, &flag_member);
	type_registry_create();
	register_symbol(&aggregate);

	type = type_lookup_exact(&aggregate);
	members = type_members(type);
	check(type != NULL, "aggregate has a locklint type");
	check(type_member_count(type) == 3,
	    "aggregate retains every member");
	check(members != NULL &&
	    members[0].representative == &value_member &&
	    members[1].representative == &next_member &&
	    members[2].representative == &flag_member,
	    "canonical members retain declaration order and names");
	check(members[0].representative->offset == 0 &&
	    members[1].representative->offset == 8 &&
	    members[2].representative->offset == 16,
	    "canonical members retain byte offsets");
	check(is_bitfield_type(members[2].representative) &&
	    members[2].representative->bit_offset == 5 &&
	    members[2].representative->bit_size == 3,
	    "canonical member retains bit-field layout");
	check(members[1].type == type_lookup_exact(&self_pointer),
	    "recursive pointer member uses its canonical type");
	check(type_member_lookup_exact(&value_member) == &members[0] &&
	    type_member_lookup_exact(&next_member) == &members[1] &&
	    type_member_lookup_exact(&flag_member) == &members[2],
	    "exact Sparse members map to canonical members");

	type_registry_destroy();
	free_ptr_list(&aggregate.symbol_list);
}

int
main(void)
{
	test_type_indexes();
	test_type_shapes();
	test_aggregate_members();
	if (failures != 0) {
		(void) fprintf(stderr, "%u test failure%s\n", failures,
		    failures == 1 ? "" : "s");
		return (1);
	}
	return (0);
}
