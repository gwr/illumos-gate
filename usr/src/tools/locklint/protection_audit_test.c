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
 * Exercise protection-audit datum classification and ordering independently
 * of result collection and report rendering.
 */

#include <stdbool.h>
#include <stdio.h>

#include "access.h"
#include "events.h"
#include "function_info.h"
#include "identity.h"
#include "lib.h"
#include "linearize.h"
#include "protection_audit.h"
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

static void
register_type(struct symbol *type)
{
	struct symbol declaration = {
		.type = SYM_NODE,
		.ctype = { .base_type = type }
	};
	struct symbol_list *symbols = NULL;

	add_symbol(&symbols, &declaration);
	type_symbols_register(symbols);
	free_ptr_list(&symbols);
}

static void
test_datum_keys(void)
{
	struct stream streams[] = {
		{ .name = "audit.h" }
	};
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
	struct symbol aggregate = {
		.type = SYM_STRUCT,
		.namespace = NS_STRUCT,
		.pos = { .stream = 0, .line = 10, .pos = 1 },
		.ident = built_in_ident("audit_record"),
		.bit_size = 32,
		.examined = 1,
		.ctype = { .alignment = 4 }
	};
	struct symbol member = {
		.type = SYM_NODE,
		.ident = built_in_ident("value"),
		.bit_size = 32,
		.examined = 1,
		.ctype = { .base_type = &integer }
	};
	struct symbol pointer = {
		.type = SYM_PTR,
		.bit_size = 64,
		.examined = 1,
		.ctype = { .alignment = 8, .base_type = &aggregate }
	};
	struct symbol external_symbol = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.ident = built_in_ident("alpha"),
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_TOPLEVEL,
			.base_type = &integer
		}
	};
	struct symbol later_external_symbol = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.ident = built_in_ident("omega"),
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_TOPLEVEL,
			.base_type = &integer
		}
	};
	struct symbol internal_symbol = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.ident = built_in_ident("internal"),
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_TOPLEVEL | MOD_STATIC,
			.base_type = &integer
		}
	};
	struct symbol local = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.ident = built_in_ident("local"),
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_AUTO,
			.base_type = &integer
		}
	};
	struct symbol local_static = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.pos = { .stream = 0, .line = 30, .pos = 2 },
		.ident = built_in_ident("saved"),
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_STATIC,
			.base_type = &integer
		}
	};
	struct symbol formal = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.ident = built_in_ident("record"),
		.bit_size = 64,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_AUTO,
			.base_type = &pointer
		}
	};
	struct symbol tls = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.ident = built_in_ident("thread_value"),
		.bit_size = 32,
		.examined = 1,
		.ctype = {
			.modifiers = MOD_TOPLEVEL | MOD_TLS,
			.base_type = &integer
		}
	};
	struct symbol function_symbol = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.pos = { .stream = 0, .line = 20, .pos = 1 },
		.ident = built_in_ident("update"),
		.examined = 1
	};
	struct entrypoint ep = {
		.name = &function_symbol
	};
	struct function_info function = {
		.ep = &ep
	};
	struct pseudo direct = {
		.type = PSEUDO_SYM,
		.sym = &local
	};
	struct pseudo indirect = {
		.type = PSEUDO_REG
	};
	struct translation_unit *tu;
	struct locklint_access access = { 0 };
	struct protection_audit_datum_key external;
	struct protection_audit_datum_key later_external;
	struct protection_audit_datum_key internal;
	struct protection_audit_datum_key structural;
	struct protection_audit_datum_key function_static;
	struct protection_audit_datum_key other;
	enum protection_audit_key_result result;

	input_streams = streams;
	input_stream_nr = 0;
	add_symbol(&aggregate.symbol_list, &member);
	type_registry_create();
	register_type(&aggregate);
	tu = locklint_translation_unit_begin("audit.c");
	function.tu = tu;

	access.root = &external_symbol;
	access.object = locklint_object_identity(tu, &external_symbol);
	access.type = &integer;
	access.address_base = &direct;
	access.address_base_is_symbol = true;
	result = protection_audit_datum_key_init(&external, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_OK &&
	    external.kind == PROTECTION_AUDIT_DATUM_EXTERNAL,
	    "external object datum");

	access.root = &later_external_symbol;
	access.object = locklint_object_identity(tu, &later_external_symbol);
	result = protection_audit_datum_key_init(&later_external, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_OK &&
	    protection_audit_datum_report_compare(&external,
	    &later_external) < 0, "external objects sort by name");

	access.root = &internal_symbol;
	access.object = locklint_object_identity(tu, &internal_symbol);
	result = protection_audit_datum_key_init(&internal, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_OK &&
	    internal.kind == PROTECTION_AUDIT_DATUM_INTERNAL &&
	    protection_audit_datum_report_compare(&external, &internal) < 0,
	    "internal object follows external object");

	access.root = &local;
	access.object = NULL;
	access.address_base = &direct;
	result = protection_audit_datum_key_init(&other, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_EXCLUDED,
	    "direct automatic object is excluded");

	access.root = &tls;
	access.object = locklint_object_identity(tu, &tls);
	result = protection_audit_datum_key_init(&other, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_EXCLUDED,
	    "thread-local object is excluded");

	access.root = &formal;
	access.object = NULL;
	access.type = &aggregate;
	access.member = &member;
	access.address_base = &indirect;
	access.address_base_is_symbol = false;
	result = protection_audit_datum_key_init(&structural, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_OK &&
	    structural.kind == PROTECTION_AUDIT_DATUM_STRUCTURAL &&
	    structural.type == type_lookup_exact(&aggregate),
	    "formal pointee uses canonical structural datum");

	access.address_base = &direct;
	access.address_base_is_symbol = true;
	result = protection_audit_datum_key_init(&other, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_EXCLUDED,
	    "formal parameter object itself is excluded");

	access.root = &local_static;
	access.type = &integer;
	access.member = NULL;
	access.address_base = &direct;
	access.address_base_is_symbol = true;
	result = protection_audit_datum_key_init(&function_static, tu,
	    &function, &access);
	check(result == PROTECTION_AUDIT_KEY_OK &&
	    function_static.kind == PROTECTION_AUDIT_DATUM_LOCAL_STATIC,
	    "function-local static is retained");
	check(protection_audit_datum_report_compare(&structural,
	    &function_static) < 0, "structural datum precedes local static");

	access.root = &formal;
	access.type = &integer;
	access.address_base = &indirect;
	access.address_base_is_symbol = false;
	result = protection_audit_datum_key_init(&other, tu, &function,
	    &access);
	check(result == PROTECTION_AUDIT_KEY_UNSUPPORTED,
	    "non-structural pointee awaits a provenance identity");

	other = external;
	check(protection_audit_datum_identity_compare(&external, &other) == 0,
	    "copied datum key retains identity");
	other.offset++;
	check(protection_audit_datum_identity_compare(&external, &other) != 0,
	    "region offset participates in datum identity");

	type_registry_destroy();
	free_ptr_list(&aggregate.symbol_list);
}

static void
test_candidates(void)
{
	struct stream streams[] = {
		{ .name = "candidates.h" }
	};
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
	struct ident *tag = built_in_ident("candidate_record");
	struct symbol aggregate = {
		.type = SYM_STRUCT,
		.namespace = NS_STRUCT,
		.pos = { .stream = 0, .line = 10, .pos = 1 },
		.ident = tag,
		.bit_size = 128,
		.examined = 1,
		.ctype = { .alignment = 4 }
	};
	struct symbol lock_member = {
		.type = SYM_NODE,
		.ident = built_in_ident("lock"),
		.bit_size = 32,
		.examined = 1,
		.ctype = { .base_type = &integer }
	};
	struct symbol data_member = {
		.type = SYM_NODE,
		.ident = built_in_ident("value"),
		.offset = 4,
		.bit_size = 32,
		.examined = 1,
		.ctype = { .base_type = &integer }
	};
	struct symbol function_symbol = {
		.type = SYM_NODE,
		.namespace = NS_SYMBOL,
		.pos = { .stream = 0, .line = 20, .pos = 1 },
		.ident = built_in_ident("candidate_function"),
		.examined = 1
	};
	struct entrypoint ep = {
		.name = &function_symbol
	};
	struct function_info function = {
		.ep = &ep
	};
	struct locklint_access access = {
		.type = &aggregate,
		.member = &data_member,
		.offset = 20
	};
	struct lock_identity exact_lock = {
		.key = {
			.analysis_object = &exact_lock,
			.target_offset = 0
		},
		.analysis_object_type = LOCK_ANALYSIS_OBJECT_OBJECT_IDENTITY
	};
	struct lock_identity role_lock = {
		.key = {
			.analysis_object = &aggregate,
			.target_offset = 16
		},
		.analysis_object_type = LOCK_ANALYSIS_OBJECT_PSEUDO
	};
	struct lock_identity other_owner_lock = {
		.key = {
			.analysis_object = &integer,
			.target_offset = 16
		},
		.analysis_object_type = LOCK_ANALYSIS_OBJECT_PSEUDO
	};
	struct lock_identity opaque_lock = {
		.key = {
			.analysis_object = &opaque_lock,
			.target_offset = 7
		},
		.analysis_object_type = LOCK_ANALYSIS_OBJECT_PSEUDO
	};
	struct lock_identity ambiguous_merged;
	struct protection_audit_candidate exact;
	struct protection_audit_candidate proven;
	struct protection_audit_candidate unresolved;
	struct protection_audit_candidate merged;
	struct protection_audit_candidate opaque;
	struct protection_audit_candidate common;
	const struct ll_type *owner;
	const struct type_member *role;
	bool candidate;

	input_streams = streams;
	input_stream_nr = 0;
	add_symbol(&aggregate.symbol_list, &lock_member);
	add_symbol(&aggregate.symbol_list, &data_member);
	type_registry_create();
	register_type(&aggregate);
	owner = type_lookup_exact(&aggregate);
	role = type_member_lookup_exact(&lock_member);
	role_lock.role = role;
	other_owner_lock.role = role;

	candidate = protection_audit_candidate_init(&exact, &function,
	    &access, &aggregate, 20, &exact_lock, true, false,
	    LOCKLINT_MODE_MUTEX, true);
	check(candidate &&
	    exact.kind == PROTECTION_AUDIT_CANDIDATE_EXACT_OBJECT &&
	    exact.evidence == PROTECTION_AUDIT_EVIDENCE_EXACT,
	    "named object lock remains exact");
	check(!protection_audit_candidate_init(&common, &function, &access,
	    &aggregate, 20, &exact_lock, true, false, LOCKLINT_MODE_READER,
	    true),
	    "reader mode is unsuitable for a write");
	check(!protection_audit_candidate_init(&common, &function, &access,
	    &aggregate, 20, &exact_lock, true, true, LOCKLINT_MODE_MUTEX,
	    true), "thread-local lock is unsuitable for shared protection");

	candidate = protection_audit_candidate_init(&proven, &function,
	    &access, &aggregate, 20, &role_lock, false, false,
	    LOCKLINT_MODE_MUTEX, true);
	check(candidate && proven.kind == PROTECTION_AUDIT_CANDIDATE_ROLE &&
	    proven.role == role &&
	    proven.evidence == PROTECTION_AUDIT_EVIDENCE_PROVEN,
	    "same-owner role is proved");

	candidate = protection_audit_candidate_init(&unresolved, &function,
	    &access, &aggregate, 20, &other_owner_lock,
	    false, false, LOCKLINT_MODE_MUTEX, true);
	check(candidate &&
	    unresolved.kind == PROTECTION_AUDIT_CANDIDATE_ROLE &&
	    unresolved.evidence == PROTECTION_AUDIT_EVIDENCE_UNRESOLVED,
	    "different owner retains unresolved role evidence");

	check(type_merge_instances(tag) == TYPE_MERGE_INSTANCES_OK,
	    "merge candidate aggregate instances");
	role_lock.key.analysis_object = type_merged_instance(owner);
	role_lock.key.target_offset = 0;
	candidate = protection_audit_candidate_init(&merged, &function,
	    &access, &aggregate, 20, &role_lock, false, false,
	    LOCKLINT_MODE_MUTEX, true);
	check(candidate && merged.kind == PROTECTION_AUDIT_CANDIDATE_ROLE &&
	    merged.evidence == PROTECTION_AUDIT_EVIDENCE_MERGED,
	    "explicitly merged role remains visible");

	ambiguous_merged = role_lock;
	ambiguous_merged.role = NULL;
	ambiguous_merged.role_conflict = true;
	candidate = protection_audit_candidate_init(&common, &function,
	    &access, &aggregate, 20, &ambiguous_merged, false,
	    false, LOCKLINT_MODE_MUTEX, true);
	check(candidate &&
	    common.kind == PROTECTION_AUDIT_CANDIDATE_UNRESOLVED &&
	    common.evidence == PROTECTION_AUDIT_EVIDENCE_UNRESOLVED,
	    "unnamed merged identity remains unresolved");

	candidate = protection_audit_candidate_init(&opaque, &function,
	    &access, &aggregate, 20, &opaque_lock, false, false,
	    LOCKLINT_MODE_MUTEX, true);
	check(candidate &&
	    opaque.kind == PROTECTION_AUDIT_CANDIDATE_UNRESOLVED &&
	    opaque.evidence == PROTECTION_AUDIT_EVIDENCE_UNRESOLVED,
	    "opaque lock remains unresolved evidence");

	common = proven;
	check(protection_audit_candidate_combine(&common, &unresolved) &&
	    common.evidence == PROTECTION_AUDIT_EVIDENCE_UNRESOLVED,
	    "common role retains weakest evidence");
	check(!protection_audit_candidate_combine(&common, &exact),
	    "different candidate identities do not combine");
	check(protection_audit_candidate_identity_compare(&proven,
	    &unresolved) == 0, "role identity ignores evidence strength");

	type_registry_destroy();
	free_ptr_list(&aggregate.symbol_list);
}

int
main(void)
{
	test_datum_keys();
	test_candidates();
	if (failures != 0) {
		(void) fprintf(stderr, "%u test failure%s\n", failures,
		    failures == 1 ? "" : "s");
		return (1);
	}
	return (0);
}
