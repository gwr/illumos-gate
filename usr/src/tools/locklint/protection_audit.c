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
 * Classify source-level data regions for protection auditing.  Datum keys
 * retain stable analysis identities and numeric coordinates; collection and
 * rendering are deliberately separate.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "access.h"
#include "annotations.h"
#include "avl.h"
#include "context.h"
#include "events.h"
#include "expression.h"
#include "function_info.h"
#include "identity.h"
#include "lib.h"
#include "linearize.h"
#include "output.h"
#include "protection_audit.h"
#include "symbol.h"
#include "token.h"
#include "type.h"

struct protection_audit_site {
	struct protection_audit_datum_key datum;
	const struct function_info *function;
	const struct instruction *instruction;
	struct protection_audit_candidate observed[2];
	size_t observed_count;
	bool observed_truncated;
	bool read;
	bool written;
	bool no_lock;
	bool unsuitable_mode;
	avl_node_t by_site;
};

struct protection_audit_record {
	struct protection_audit_datum_key datum;
	struct protection_audit_candidate *candidates;
	size_t candidate_count;
	struct protection_audit_candidate observed[2];
	size_t observed_count;
	bool candidates_initialized;
	bool observed_truncated;
	bool unsuitable_mode;
	bool unresolved;
	bool read;
	bool written;
	size_t relevant_states;
	bool has_policy;
	struct locklint_data_policy policy;
	struct locklint_access policy_lock;
	struct protection_audit_site **sites;
	size_t site_count;
	size_t site_capacity;
	size_t total_sites;
	avl_node_t by_datum;
};

struct protection_audit_named_object {
	const struct object_identity *object;
	bool thread_local;
};

struct protection_audit_result {
	avl_tree_t records;
	struct protection_audit_named_object *objects;
	size_t object_count;
	size_t object_capacity;
	avl_tree_t function_sites;
	bool function_sites_active;
};

static struct locklint_output protection_output =
    LOCKLINT_OUTPUT_INITIALIZER("audit-protection", false);
static struct locklint_output unprotected_output =
    LOCKLINT_OUTPUT_INITIALIZER("audit-unprotected", false);
static size_t protection_audit_site_limit = 3;
static bool protection_audit_all_sites;
static bool protection_audit_site_limit_specified;

static const char *protection_audit_function_name(
    const struct function_info *);
static int protection_audit_candidate_report_compare(const void *,
    const void *);

static int
compare_uintptr(uintptr_t left, uintptr_t right)
{
	if (left < right)
		return (-1);
	if (left > right)
		return (1);
	return (0);
}

static int
compare_ulong(unsigned long left, unsigned long right)
{
	if (left < right)
		return (-1);
	if (left > right)
		return (1);
	return (0);
}

static int
compare_uint64(uint64_t left, uint64_t right)
{
	if (left < right)
		return (-1);
	if (left > right)
		return (1);
	return (0);
}

static int
compare_string(const char *left, const char *right)
{
	if (left == right)
		return (0);
	if (left == NULL)
		return (-1);
	if (right == NULL)
		return (1);
	return (strcmp(left, right));
}

static int
compare_region(const struct protection_audit_datum_key *left,
    const struct protection_audit_datum_key *right)
{
	int comparison;

	comparison = locklint_member_path_compare(left->path, right->path);
	if (comparison != 0)
		return (comparison);
	comparison = compare_ulong(left->offset, right->offset);
	if (comparison != 0)
		return (comparison);
	return (compare_uint64(left->size, right->size));
}

static int
compare_tu(const struct translation_unit *left,
    const struct translation_unit *right)
{
	int comparison;

	if (left == right)
		return (0);
	if (left == NULL)
		return (-1);
	if (right == NULL)
		return (1);
	comparison = compare_string(locklint_translation_unit_file(left),
	    locklint_translation_unit_file(right));
	if (comparison != 0)
		return (comparison);
	if (locklint_translation_unit_id(left) <
	    locklint_translation_unit_id(right))
		return (-1);
	if (locklint_translation_unit_id(left) >
	    locklint_translation_unit_id(right))
		return (1);
	return (0);
}

/*
 * An instruction access whose lowered base is its declaration names the
 * declaration's storage itself.  A non-symbol base represents a dereference
 * or derived address and must not be rejected merely because its source root
 * is a local pointer.
 */
static bool
direct_thread_private(const struct locklint_access *access)
{
	struct symbol *declared_type;

	if ((access->root->ctype.modifiers & MOD_TLS) != 0)
		return (true);
	if (access->address_base != NULL)
		return (access->address_base_is_symbol);

	declared_type = type_node_strip(access->root->ctype.base_type);
	return (declared_type == NULL || declared_type->type != SYM_PTR);
}

/*
 * Select the strongest identity available without inventing provenance.
 * Exact objects and local statics retain declaration identity; indirect
 * aggregate accesses use their canonical type and member path.  Other
 * indirect accesses remain available for later provenance and site keys.
 */
enum protection_audit_key_result
protection_audit_datum_key_init(struct protection_audit_datum_key *key,
    struct translation_unit *tu, const struct function_info *function,
    const struct locklint_access *access)
{
	const struct translation_unit *owner;
	struct symbol *compound;
	uint64_t size;

	if (key == NULL || tu == NULL || access == NULL ||
	    access->root == NULL || !locklint_access_size(access, &size))
		return (PROTECTION_AUDIT_KEY_UNSUPPORTED);
	(void) memset(key, 0, sizeof (*key));

	if ((access->root->ctype.modifiers & MOD_TLS) != 0)
		return (PROTECTION_AUDIT_KEY_EXCLUDED);

	if (access->object != NULL) {
		owner = locklint_object_owner(access->object);
		key->kind = owner == NULL ? PROTECTION_AUDIT_DATUM_EXTERNAL :
		    PROTECTION_AUDIT_DATUM_INTERNAL;
		key->object = access->object;
	} else if ((access->root->ctype.modifiers & MOD_STATIC) != 0) {
		if (function == NULL || function->tu != tu ||
		    function->ep == NULL ||
		    function->ep->name == NULL)
			return (PROTECTION_AUDIT_KEY_UNSUPPORTED);
		key->kind = PROTECTION_AUDIT_DATUM_LOCAL_STATIC;
		key->tu = tu;
		key->function = function;
		key->symbol = access->root;
	} else {
		if (direct_thread_private(access))
			return (PROTECTION_AUDIT_KEY_EXCLUDED);
		compound = type_compound_resolve(access->type);
		key->type = type_lookup_exact(compound);
		if (key->type == NULL || type_report_order(key->type) == 0)
			return (PROTECTION_AUDIT_KEY_UNSUPPORTED);
		key->kind = PROTECTION_AUDIT_DATUM_STRUCTURAL;
	}

	key->path = access->path;
	key->offset = access->offset;
	key->size = size;
	return (PROTECTION_AUDIT_KEY_OK);
}

/*
 * Identity comparison is suitable for the owning datum index.  It uses
 * canonical analysis pointers rather than display text, then distinguishes
 * regions within that identity.
 */
int
protection_audit_datum_identity_compare(
    const struct protection_audit_datum_key *left,
    const struct protection_audit_datum_key *right)
{
	int comparison;

	if (left->kind < right->kind)
		return (-1);
	if (left->kind > right->kind)
		return (1);
	switch (left->kind) {
	case PROTECTION_AUDIT_DATUM_EXTERNAL:
	case PROTECTION_AUDIT_DATUM_INTERNAL:
		comparison = compare_uintptr((uintptr_t)left->object,
		    (uintptr_t)right->object);
		break;
	case PROTECTION_AUDIT_DATUM_STRUCTURAL:
		comparison = compare_uintptr((uintptr_t)left->type,
		    (uintptr_t)right->type);
		break;
	case PROTECTION_AUDIT_DATUM_LOCAL_STATIC:
		comparison = compare_uintptr((uintptr_t)left->tu,
		    (uintptr_t)right->tu);
		if (comparison == 0)
			comparison = compare_uintptr((uintptr_t)left->function,
			    (uintptr_t)right->function);
		if (comparison == 0)
			comparison = compare_uintptr((uintptr_t)left->symbol,
			    (uintptr_t)right->symbol);
		break;
	default:
		abort();
	}
	if (comparison != 0)
		return (comparison);
	comparison = compare_uintptr((uintptr_t)left->path,
	    (uintptr_t)right->path);
	if (comparison != 0)
		return (comparison);
	comparison = compare_ulong(left->offset, right->offset);
	if (comparison != 0)
		return (comparison);
	return (compare_uint64(left->size, right->size));
}

/*
 * Report comparison follows the user-visible source hierarchy.  The identity
 * comparator is only a final total-order tie-breaker after every retained
 * deterministic source coordinate compares equal.
 */
int
protection_audit_datum_report_compare(
    const struct protection_audit_datum_key *left,
    const struct protection_audit_datum_key *right)
{
	int comparison;
	size_t left_order;
	size_t right_order;

	if (left->kind < right->kind)
		return (-1);
	if (left->kind > right->kind)
		return (1);
	switch (left->kind) {
	case PROTECTION_AUDIT_DATUM_EXTERNAL:
		comparison = compare_string(locklint_object_name(left->object),
		    locklint_object_name(right->object));
		break;
	case PROTECTION_AUDIT_DATUM_INTERNAL:
		comparison = compare_tu(locklint_object_owner(left->object),
		    locklint_object_owner(right->object));
		if (comparison == 0)
			comparison = compare_string(
			    locklint_object_name(left->object),
			    locklint_object_name(right->object));
		break;
	case PROTECTION_AUDIT_DATUM_STRUCTURAL:
		left_order = type_report_order(left->type);
		right_order = type_report_order(right->type);
		if (left_order < right_order)
			return (-1);
		if (left_order > right_order)
			return (1);
		comparison = compare_uintptr((uintptr_t)left->type,
		    (uintptr_t)right->type);
		break;
	case PROTECTION_AUDIT_DATUM_LOCAL_STATIC:
		comparison = compare_tu(left->tu, right->tu);
		if (comparison == 0) {
			struct symbol *left_function = left->function->ep->name;
			struct symbol *right_function = right->function->ep->name;

			if (left_function->pos.stream <
			    right_function->pos.stream)
				comparison = -1;
			else if (left_function->pos.stream >
			    right_function->pos.stream)
				comparison = 1;
			else if (left_function->pos.line <
			    right_function->pos.line)
				comparison = -1;
			else if (left_function->pos.line >
			    right_function->pos.line)
				comparison = 1;
			else if (left_function->pos.pos <
			    right_function->pos.pos)
				comparison = -1;
			else if (left_function->pos.pos >
			    right_function->pos.pos)
				comparison = 1;
			else
				comparison = compare_string(
				    show_ident(left_function->ident),
				    show_ident(right_function->ident));
		}
		if (comparison == 0) {
			if (left->symbol->pos.stream < right->symbol->pos.stream)
				comparison = -1;
			else if (left->symbol->pos.stream >
			    right->symbol->pos.stream)
				comparison = 1;
			else if (left->symbol->pos.line <
			    right->symbol->pos.line)
				comparison = -1;
			else if (left->symbol->pos.line >
			    right->symbol->pos.line)
				comparison = 1;
			else if (left->symbol->pos.pos <
			    right->symbol->pos.pos)
				comparison = -1;
			else if (left->symbol->pos.pos >
			    right->symbol->pos.pos)
				comparison = 1;
			else
				comparison = compare_string(
				    show_ident(left->symbol->ident),
				    show_ident(right->symbol->ident));
		}
		break;
	default:
		abort();
	}
	if (comparison != 0)
		return (comparison);
	comparison = compare_region(left, right);
	if (comparison != 0)
		return (comparison);
	return (protection_audit_datum_identity_compare(left, right));
}

static bool
candidate_same_owner(const struct locklint_access *access,
    const void *datum_object, int64_t datum_offset,
    const struct lock_identity *lock, const struct type_member *role)
{
	const struct ll_type *owner = type_member_owner(role);
	uint64_t relative;
	unsigned long source_base;
	unsigned long role_offset;
	int64_t owner_offset;

	if (owner == NULL ||
	    !locklint_access_containing_base_canonical(access, owner,
	    access->offset, &source_base) ||
	    source_base > access->offset)
		return (false);
	relative = (uint64_t)access->offset - source_base;
	role_offset = role->representative->offset;
	if (relative > INT64_MAX || role_offset > INT64_MAX ||
	    datum_offset < INT64_MIN + (int64_t)relative)
		return (false);
	owner_offset = datum_offset - (int64_t)relative;
	if (owner_offset > INT64_MAX - (int64_t)role_offset)
		return (false);
	return (lock->key.analysis_object == datum_object &&
	    lock->key.target_offset ==
	    owner_offset + (int64_t)role_offset);
}

/*
 * Preserve every suitably held lock at the strongest source evidence
 * available in this state.  Exact named objects remain exact; canonical roles
 * identify merged, proved same-owner, or unresolved structural evidence.
 */
bool
protection_audit_candidate_init(struct protection_audit_candidate *candidate,
    const struct function_info *function,
    const struct locklint_access *datum_access, const void *datum_object,
    int64_t datum_offset, const struct lock_identity *lock,
    bool lock_is_named_object, bool lock_is_thread_local, unsigned int modes,
    bool written)
{
	const struct type_member *role;
	const struct ll_type *owner;
	const void *merged_instance;
	unsigned int suitable_modes = written ?
	    LOCKLINT_MODE_MUTEX | LOCKLINT_MODE_WRITER :
	    LOCKLINT_MODE_MUTEX | LOCKLINT_MODE_READER | LOCKLINT_MODE_WRITER;

	if (candidate == NULL || datum_access == NULL || datum_object == NULL ||
	    lock == NULL || lock_is_thread_local ||
	    (modes & suitable_modes) == 0)
		return (false);
	(void) memset(candidate, 0, sizeof (*candidate));
	candidate->observed_modes = modes & suitable_modes;

	role = !lock->role_conflict ? lock->role : NULL;
	owner = type_member_owner(role);
	merged_instance = type_merged_instance(owner);
	if (role != NULL && merged_instance != NULL &&
	    lock->key.analysis_object == merged_instance) {
		candidate->kind = PROTECTION_AUDIT_CANDIDATE_ROLE;
		candidate->evidence = PROTECTION_AUDIT_EVIDENCE_MERGED;
		candidate->identity = role;
		candidate->role = role;
		return (true);
	}
	if (lock_is_named_object && lock->analysis_object_type ==
	    LOCK_ANALYSIS_OBJECT_OBJECT_IDENTITY) {
		candidate->kind = PROTECTION_AUDIT_CANDIDATE_EXACT_OBJECT;
		candidate->evidence = PROTECTION_AUDIT_EVIDENCE_EXACT;
		candidate->identity = lock->key.analysis_object;
		candidate->role = role;
		candidate->offset = lock->key.target_offset;
		return (true);
	}
	if (role != NULL) {
		candidate->kind = PROTECTION_AUDIT_CANDIDATE_ROLE;
		candidate->evidence = candidate_same_owner(datum_access,
		    datum_object, datum_offset, lock, role) ?
		    PROTECTION_AUDIT_EVIDENCE_PROVEN :
		    PROTECTION_AUDIT_EVIDENCE_UNRESOLVED;
		candidate->identity = role;
		candidate->role = role;
		return (true);
	}
	if (lock->analysis_object_type == LOCK_ANALYSIS_OBJECT_SYMBOL &&
	    lock->key.analysis_object != NULL &&
	    (((const struct symbol *)lock->key.analysis_object)->
	    ctype.modifiers & MOD_STATIC) != 0) {
		candidate->kind = PROTECTION_AUDIT_CANDIDATE_EXACT_STATIC;
		candidate->evidence = PROTECTION_AUDIT_EVIDENCE_EXACT;
		candidate->identity = lock->key.analysis_object;
		candidate->function = function;
		candidate->offset = lock->key.target_offset;
		return (true);
	}
	candidate->kind = PROTECTION_AUDIT_CANDIDATE_UNRESOLVED;
	candidate->evidence = PROTECTION_AUDIT_EVIDENCE_UNRESOLVED;
	candidate->identity = lock->key.analysis_object;
	candidate->function = function;
	candidate->offset = lock->key.target_offset;
	return (true);
}

int
protection_audit_candidate_identity_compare(
    const struct protection_audit_candidate *left,
    const struct protection_audit_candidate *right)
{
	int comparison;

	if (left->kind < right->kind)
		return (-1);
	if (left->kind > right->kind)
		return (1);
	comparison = compare_uintptr((uintptr_t)left->identity,
	    (uintptr_t)right->identity);
	if (comparison != 0)
		return (comparison);
	if (left->kind == PROTECTION_AUDIT_CANDIDATE_EXACT_STATIC) {
		comparison = compare_uintptr((uintptr_t)left->function,
		    (uintptr_t)right->function);
		if (comparison != 0)
			return (comparison);
	}
	return (left->offset < right->offset ? -1 :
	    left->offset > right->offset ? 1 : 0);
}

/*
 * Combining observations never strengthens their shared conclusion.  Modes
 * accumulate for later rendering, while identity remains the intersection
 * key and evidence falls to the weakest observation.
 */
bool
protection_audit_candidate_combine(
    struct protection_audit_candidate *retained,
    const struct protection_audit_candidate *observation)
{
	if (protection_audit_candidate_identity_compare(retained,
	    observation) != 0)
		return (false);
	if (observation->evidence < retained->evidence)
		retained->evidence = observation->evidence;
	retained->observed_modes |= observation->observed_modes;
	return (true);
}

unsigned int
protection_audit_note_flags(bool written, size_t common_count,
    size_t observed_count, bool unsuitable_mode, bool unresolved)
{
	unsigned int notes = 0;

	if (common_count != 0) {
		if (unresolved)
			notes |= PROTECTION_AUDIT_NOTE_UNRESOLVED;
		return (notes);
	}
	if (observed_count == 0) {
		if (written)
			notes |= PROTECTION_AUDIT_NOTE_NONE;
		return (notes);
	}
	if (observed_count == 1)
		notes |= PROTECTION_AUDIT_NOTE_INCONSISTENT;
	else
		notes |= PROTECTION_AUDIT_NOTE_VARYING;
	if (unsuitable_mode)
		notes |= PROTECTION_AUDIT_NOTE_MODE;
	if (unresolved)
		notes |= PROTECTION_AUDIT_NOTE_UNRESOLVED;
	return (notes);
}

static int
protection_audit_record_compare(const void *left_arg, const void *right_arg)
{
	const struct protection_audit_record *left = left_arg;
	const struct protection_audit_record *right = right_arg;

	return (protection_audit_datum_identity_compare(&left->datum,
	    &right->datum));
}

static struct position
protection_audit_site_position(const struct protection_audit_site *site)
{
	return (site->instruction->access != NULL ?
	    site->instruction->access->pos : site->instruction->pos);
}

static int
protection_audit_site_report_compare(const void *left_arg,
    const void *right_arg)
{
	const struct protection_audit_site *const *left = left_arg;
	const struct protection_audit_site *const *right = right_arg;
	struct position left_position = protection_audit_site_position(*left);
	struct position right_position = protection_audit_site_position(*right);
	int comparison;

	comparison = compare_string(stream_name(left_position.stream),
	    stream_name(right_position.stream));
	if (comparison != 0)
		return (comparison);
	if (left_position.line < right_position.line)
		return (-1);
	if (left_position.line > right_position.line)
		return (1);
	if (left_position.pos < right_position.pos)
		return (-1);
	if (left_position.pos > right_position.pos)
		return (1);
	return (compare_string(
	    protection_audit_function_name((*left)->function),
	    protection_audit_function_name((*right)->function)));
}

static int
protection_audit_site_compare(const void *left_arg, const void *right_arg)
{
	const struct protection_audit_site *left = left_arg;
	const struct protection_audit_site *right = right_arg;
	int comparison;

	comparison = protection_audit_datum_identity_compare(&left->datum,
	    &right->datum);
	if (comparison != 0)
		return (comparison);
	return (compare_uintptr((uintptr_t)left->instruction,
	    (uintptr_t)right->instruction));
}

static int
protection_audit_named_object_compare(const void *left_arg,
    const void *right_arg)
{
	const struct protection_audit_named_object *left = left_arg;
	const struct protection_audit_named_object *right = right_arg;

	return (compare_uintptr((uintptr_t)left->object,
	    (uintptr_t)right->object));
}

static void
protection_audit_add_named_object(const struct object_identity *object,
    void *result_arg)
{
	struct protection_audit_result *result = result_arg;
	struct protection_audit_named_object *entry;
	const struct symbol *representative;
	size_t capacity;

	if (result->object_count == result->object_capacity) {
		capacity = result->object_capacity != 0 ?
		    result->object_capacity * 2 : 64;
		if (capacity < result->object_capacity ||
		    capacity > SIZE_MAX / sizeof (*result->objects))
			die("protection audit object allocation overflow");
		result->objects = realloc(result->objects,
		    capacity * sizeof (*result->objects));
		if (result->objects == NULL)
			die("cannot allocate protection audit object index");
		result->object_capacity = capacity;
	}
	entry = &result->objects[result->object_count++];
	representative = locklint_object_representative(object);
	*entry = (struct protection_audit_named_object) {
		.object = object,
		.thread_local = representative != NULL &&
		    (representative->ctype.modifiers & MOD_TLS) != 0
	};
}

static bool
protection_audit_named_object(const struct protection_audit_result *result,
    const void *identity, bool *thread_local)
{
	struct protection_audit_named_object key = {
		.object = identity
	};
	struct protection_audit_named_object *entry;

	entry = bsearch(&key, result->objects, result->object_count,
	    sizeof (*result->objects), protection_audit_named_object_compare);
	if (entry == NULL)
		return (false);
	*thread_local = entry->thread_local;
	return (true);
}

void
protection_audit_output_register(void)
{
	locklint_output_register(&protection_output);
	locklint_output_register(&unprotected_output);
}

bool
protection_audit_option(const char *argument)
{
	const char *prefix = "--audit-site-limit=";
	const char *value;
	char *end;
	unsigned long long limit;

	if (locklint_output_option(argument, &protection_output) ||
	    locklint_output_option(argument, &unprotected_output))
		return (true);
	if (strncmp(argument, prefix, strlen(prefix)) != 0)
		return (false);
	value = argument + strlen(prefix);
	protection_audit_site_limit_specified = true;
	if (strcmp(value, "all") == 0) {
		protection_audit_all_sites = true;
		return (true);
	}
	errno = 0;
	limit = strtoull(value, &end, 10);
	if (errno != 0 || *value == '\0' || *end != '\0' || limit == 0 ||
	    limit > SIZE_MAX)
		die("audit-site-limit requires a positive number or 'all'");
	protection_audit_site_limit = (size_t)limit;
	protection_audit_all_sites = false;
	return (true);
}

bool
protection_audit_is_enabled(void)
{
	return (locklint_output_is_enabled(&protection_output) ||
	    locklint_output_is_enabled(&unprotected_output));
}

void
protection_audit_options_validate(void)
{
	if (protection_audit_site_limit_specified &&
	    !locklint_output_is_enabled(&unprotected_output))
		die("audit-site-limit requires --audit-unprotected");
}

struct protection_audit_result *
protection_audit_result_create(void)
{
	struct protection_audit_result *result;

	result = calloc(1, sizeof (*result));
	if (result == NULL)
		die("cannot allocate protection audit result");
	avl_create(&result->records, protection_audit_record_compare,
	    sizeof (struct protection_audit_record),
	    offsetof(struct protection_audit_record, by_datum));
	locklint_for_each_object(protection_audit_add_named_object, result);
	qsort(result->objects, result->object_count, sizeof (*result->objects),
	    protection_audit_named_object_compare);
	return (result);
}

void
protection_audit_result_begin_function(struct protection_audit_result *result)
{
	if (!locklint_output_is_enabled(&unprotected_output))
		return;
	if (result->function_sites_active)
		abort();
	avl_create(&result->function_sites, protection_audit_site_compare,
	    sizeof (struct protection_audit_site),
	    offsetof(struct protection_audit_site, by_site));
	result->function_sites_active = true;
}

static void
protection_audit_record_add_site(struct protection_audit_record *record,
    struct protection_audit_site *site)
{
	size_t limit = protection_audit_all_sites ? SIZE_MAX :
	    protection_audit_site_limit;

	record->total_sites++;
	if (record->site_count < limit) {
		if (record->site_count == record->site_capacity) {
			size_t capacity = record->site_capacity != 0 ?
			    record->site_capacity * 2 : 4;

			if (capacity > limit)
				capacity = limit;
			if (capacity < record->site_capacity ||
			    capacity > SIZE_MAX / sizeof (*record->sites))
				die("protection audit site allocation overflow");
			record->sites = realloc(record->sites,
			    capacity * sizeof (*record->sites));
			if (record->sites == NULL)
				die("cannot allocate protection audit sites");
			record->site_capacity = capacity;
		}
		record->sites[record->site_count++] = site;
		return;
	}
	if (record->site_count != 0) {
		struct protection_audit_site *candidate = site;
		size_t latest = 0;
		size_t index;

		for (index = 1; index < record->site_count; index++) {
			if (protection_audit_site_report_compare(
			    &record->sites[latest], &record->sites[index]) < 0)
				latest = index;
		}
		if (protection_audit_site_report_compare(&candidate,
		    &record->sites[latest]) < 0) {
			free(record->sites[latest]);
			record->sites[latest] = site;
			return;
		}
	}
	free(site);
}

void
protection_audit_result_end_function(struct protection_audit_result *result)
{
	struct protection_audit_site *site;
	void *cookie = NULL;

	if (!result->function_sites_active)
		return;
	while ((site = avl_destroy_nodes(&result->function_sites,
	    &cookie)) != NULL) {
		struct protection_audit_record lookup = {
			.datum = site->datum
		};
		struct protection_audit_record *record =
		    avl_find(&result->records, &lookup, NULL);

		if (record == NULL)
			abort();
		protection_audit_record_add_site(record, site);
	}
	avl_destroy(&result->function_sites);
	result->function_sites_active = false;
}

static int
protection_audit_candidate_qsort(const void *left_arg, const void *right_arg)
{
	return (protection_audit_candidate_identity_compare(left_arg,
	    right_arg));
}

static size_t
protection_audit_state_candidates(struct protection_audit_result *result,
    const struct function_info *function,
    const struct locklint_access *datum_access, const void *datum_object,
    int64_t datum_offset, const struct semantic_state *state, bool written,
    struct protection_audit_candidate *candidates)
{
	size_t count = 0;
	size_t index;

	for (index = 0; index < context_state_lock_count(state); index++) {
		const struct lock_identity *lock;
		struct protection_audit_candidate candidate;
		bool named_object;
		bool thread_local = false;
		unsigned int modes;

		if (!context_state_lock_at(state, index, &lock, &modes))
			abort();
		named_object = protection_audit_named_object(result,
		    lock->key.analysis_object, &thread_local);
		if (lock->analysis_object_type ==
		    LOCK_ANALYSIS_OBJECT_SYMBOL) {
			const struct symbol *symbol =
			    lock->key.analysis_object;

			thread_local =
			    (symbol->ctype.modifiers & MOD_TLS) != 0;
		}
		if (!protection_audit_candidate_init(&candidate, function,
		    datum_access, datum_object, datum_offset, lock,
		    named_object, thread_local, modes, written))
			continue;
		candidates[count++] = candidate;
	}
	if (count > 1) {
		size_t source;
		size_t target = 0;

		qsort(candidates, count, sizeof (*candidates),
		    protection_audit_candidate_qsort);
		for (source = 0; source < count; source++) {
			if (target != 0 &&
			    protection_audit_candidate_identity_compare(
			    &candidates[target - 1], &candidates[source]) == 0) {
				if (candidates[source].evidence <
				    candidates[target - 1].evidence)
					candidates[target - 1] = candidates[source];
				else
					candidates[target - 1].observed_modes |=
					    candidates[source].observed_modes;
			} else {
				candidates[target++] = candidates[source];
			}
		}
		count = target;
	}
	return (count);
}

/*
 * Retain the two lowest report-ordered observed identities.  Two candidates
 * are sufficient to distinguish one intermittently held lock from varying
 * locks; the truncation flag records that more identities existed.
 */
static void
protection_audit_observe_candidate(struct protection_audit_record *record,
    const struct protection_audit_candidate *candidate)
{
	size_t index;

	if (candidate->evidence == PROTECTION_AUDIT_EVIDENCE_UNRESOLVED)
		record->unresolved = true;
	for (index = 0; index < record->observed_count; index++) {
		if (protection_audit_candidate_identity_compare(
		    &record->observed[index], candidate) != 0)
			continue;
		(void) protection_audit_candidate_combine(
		    &record->observed[index], candidate);
		return;
	}
	if (record->observed_count < 2) {
		record->observed[record->observed_count++] = *candidate;
	} else {
		record->observed_truncated = true;
		if (protection_audit_candidate_report_compare(candidate,
		    &record->observed[record->observed_count - 1]) >= 0)
			return;
		record->observed[record->observed_count - 1] = *candidate;
	}
	if (record->observed_count == 2 &&
	    protection_audit_candidate_report_compare(&record->observed[0],
	    &record->observed[1]) > 0) {
		struct protection_audit_candidate temporary =
		    record->observed[0];

		record->observed[0] = record->observed[1];
		record->observed[1] = temporary;
	}
}

static void
protection_audit_observe_candidates(struct protection_audit_record *record,
    const struct protection_audit_candidate *observed, size_t observed_count,
    const struct protection_audit_candidate *suitable, size_t suitable_count)
{
	size_t index;
	size_t suitable_index;

	for (index = 0; index < observed_count; index++) {
		bool found = false;

		protection_audit_observe_candidate(record, &observed[index]);
		for (suitable_index = 0; suitable_index < suitable_count;
		    suitable_index++) {
			if (protection_audit_candidate_identity_compare(
			    &observed[index], &suitable[suitable_index]) == 0) {
				found = true;
				break;
			}
		}
		if (!found)
			record->unsuitable_mode = true;
	}
}

static void
protection_audit_site_observe_candidate(struct protection_audit_site *site,
    const struct protection_audit_candidate *candidate)
{
	size_t index;

	for (index = 0; index < site->observed_count; index++) {
		if (protection_audit_candidate_identity_compare(
		    &site->observed[index], candidate) != 0)
			continue;
		(void) protection_audit_candidate_combine(
		    &site->observed[index], candidate);
		return;
	}
	if (site->observed_count < 2) {
		site->observed[site->observed_count++] = *candidate;
	} else {
		site->observed_truncated = true;
		if (protection_audit_candidate_report_compare(candidate,
		    &site->observed[site->observed_count - 1]) >= 0)
			return;
		site->observed[site->observed_count - 1] = *candidate;
	}
	if (site->observed_count == 2 &&
	    protection_audit_candidate_report_compare(&site->observed[0],
	    &site->observed[1]) > 0) {
		struct protection_audit_candidate temporary = site->observed[0];

		site->observed[0] = site->observed[1];
		site->observed[1] = temporary;
	}
}

static void
protection_audit_observe_site(struct protection_audit_result *result,
    const struct protection_audit_datum_key *datum,
    const struct function_info *function, const struct instruction *instruction,
    bool written, const struct protection_audit_candidate *observed,
    size_t observed_count, const struct protection_audit_candidate *suitable,
    size_t suitable_count)
{
	struct protection_audit_site lookup = {
		.datum = *datum,
		.function = function,
		.instruction = instruction
	};
	struct protection_audit_site *site;
	size_t index;
	size_t suitable_index;
	avl_index_t where;

	if (!result->function_sites_active)
		return;
	site = avl_find(&result->function_sites, &lookup, &where);
	if (site == NULL) {
		site = calloc(1, sizeof (*site));
		if (site == NULL)
			die("cannot allocate protection audit site");
		site->datum = *datum;
		site->function = function;
		site->instruction = instruction;
		avl_insert(&result->function_sites, site, where);
	}
	site->read |= !written;
	site->written |= written;
	if (observed_count == 0)
		site->no_lock = true;
	for (index = 0; index < observed_count; index++) {
		bool found = false;

		protection_audit_site_observe_candidate(site, &observed[index]);
		for (suitable_index = 0; suitable_index < suitable_count;
		    suitable_index++) {
			if (protection_audit_candidate_identity_compare(
			    &observed[index], &suitable[suitable_index]) == 0) {
				found = true;
				break;
			}
		}
		if (!found)
			site->unsuitable_mode = true;
	}
}

static void
protection_audit_intersect(struct protection_audit_record *record,
    const struct protection_audit_candidate *observed, size_t observed_count)
{
	size_t left = 0;
	size_t right = 0;
	size_t retained = 0;

	while (left < record->candidate_count && right < observed_count) {
		int comparison = protection_audit_candidate_identity_compare(
		    &record->candidates[left], &observed[right]);

		if (comparison < 0) {
			left++;
		} else if (comparison > 0) {
			right++;
		} else {
			record->candidates[retained] = record->candidates[left];
			(void) protection_audit_candidate_combine(
			    &record->candidates[retained], &observed[right]);
			retained++;
			left++;
			right++;
		}
	}
	record->candidate_count = retained;
}

void
protection_audit_result_observe(struct protection_audit_result *result,
    const struct function_info *function,
    const struct locklint_access *access, const struct instruction *instruction,
    const void *datum_object, int64_t datum_offset,
    const struct semantic_state *state, bool written)
{
	struct protection_audit_candidate candidates[LOCKLINT_MAX_TRACKED_LOCKS];
	struct protection_audit_candidate observed[LOCKLINT_MAX_TRACKED_LOCKS];
	struct protection_audit_record lookup = { 0 };
	struct protection_audit_record *record;
	size_t candidate_count;
	size_t observed_count;
	avl_index_t where;

	if (protection_audit_datum_key_init(&lookup.datum, function->tu,
	    function, access) != PROTECTION_AUDIT_KEY_OK)
		return;
	record = avl_find(&result->records, &lookup, &where);
	if (record == NULL) {
		record = calloc(1, sizeof (*record));
		if (record == NULL)
			die("cannot allocate protection audit record");
		record->datum = lookup.datum;
		record->has_policy = locklint_data_policy(access,
		    &record->policy, &record->policy_lock);
		avl_insert(&result->records, record, where);
	}
	record->read |= !written;
	record->written |= written;
	record->relevant_states++;
	candidate_count = protection_audit_state_candidates(result, function,
	    access, datum_object, datum_offset, state, written, candidates);
	if (written) {
		observed_count = protection_audit_state_candidates(result,
		    function, access, datum_object, datum_offset, state, false,
		    observed);
	} else {
		(void) memcpy(observed, candidates,
		    candidate_count * sizeof (*observed));
		observed_count = candidate_count;
	}
	protection_audit_observe_site(result, &record->datum, function,
	    instruction, written, observed, observed_count, candidates,
	    candidate_count);

	/*
	 * Readable policy removes reads from the inferred protection
	 * intersection.  Retain their access and site evidence, but infer the
	 * common lock from writes, which remain protected.
	 */
	if (!written && record->has_policy &&
	    record->policy.readable_without_lock)
		return;

	protection_audit_observe_candidates(record, observed, observed_count,
	    candidates, candidate_count);
	if (!record->candidates_initialized) {
		record->candidates_initialized = true;
		if (candidate_count != 0) {
			record->candidates = malloc(candidate_count *
			    sizeof (*record->candidates));
			if (record->candidates == NULL)
				die("cannot allocate protection audit candidates");
			(void) memcpy(record->candidates, candidates,
			    candidate_count * sizeof (*record->candidates));
		}
		record->candidate_count = candidate_count;
	} else if (record->candidate_count != 0) {
		protection_audit_intersect(record, candidates, candidate_count);
	}
}

static int
protection_audit_record_report_compare(const void *left_arg,
    const void *right_arg)
{
	const struct protection_audit_record *const *left = left_arg;
	const struct protection_audit_record *const *right = right_arg;

	return (protection_audit_datum_report_compare(&(*left)->datum,
	    &(*right)->datum));
}

static const char *
protection_audit_type_name(const struct ll_type *type)
{
	struct symbol *representative = type_representative(type);

	if (representative == NULL || representative->ident == NULL)
		return ("<anonymous type>");
	return (show_ident(representative->ident));
}

static const char *
protection_audit_function_name(const struct function_info *function)
{
	if (function == NULL || function->ep == NULL ||
	    function->ep->name == NULL || function->ep->name->ident == NULL)
		return ("<anonymous function>");
	return (show_ident(function->ep->name->ident));
}

static void
protection_audit_print_member(FILE *stream, const char *separator,
    const struct locklint_member_path *path)
{
	char *name;

	if (path == NULL)
		return;
	name = locklint_member_path_name(path);
	(void) fprintf(stream, "%s%s", separator, name);
	free(name);
}

static void
protection_audit_print_datum(FILE *stream,
    const struct protection_audit_datum_key *datum)
{
	switch (datum->kind) {
	case PROTECTION_AUDIT_DATUM_EXTERNAL:
		(void) fprintf(stream, "%s",
		    locklint_object_name(datum->object));
		break;
	case PROTECTION_AUDIT_DATUM_INTERNAL:
		(void) fprintf(stream, "%s::%s",
		    locklint_translation_unit_file(
		    locklint_object_owner(datum->object)),
		    locklint_object_name(datum->object));
		break;
	case PROTECTION_AUDIT_DATUM_STRUCTURAL:
		(void) fprintf(stream, "%s",
		    protection_audit_type_name(datum->type));
		protection_audit_print_member(stream, "::", datum->path);
		break;
	case PROTECTION_AUDIT_DATUM_LOCAL_STATIC:
		(void) fprintf(stream, "%s::%s",
		    protection_audit_function_name(datum->function),
		    datum->symbol->ident != NULL ?
		    show_ident(datum->symbol->ident) :
		    "<anonymous static>");
		break;
	}
	if (datum->kind != PROTECTION_AUDIT_DATUM_STRUCTURAL)
		protection_audit_print_member(stream, ".", datum->path);
	if (datum->offset > locklint_member_path_offset(datum->path)) {
		(void) fprintf(stream, "+%lu",
		    datum->offset - locklint_member_path_offset(datum->path));
	}
}

static void
protection_audit_print_role(FILE *stream, const struct type_member *role)
{
	struct symbol *member = role->representative;

	(void) fprintf(stream, "%s.%s",
	    protection_audit_type_name(type_member_owner(role)),
	    member != NULL && member->ident != NULL ?
	    show_ident(member->ident) : "<anonymous member>");
}

static void
protection_audit_print_candidate(FILE *stream,
    const struct protection_audit_candidate *candidate)
{
	const struct object_identity *object;
	const struct symbol *symbol;

	switch (candidate->kind) {
	case PROTECTION_AUDIT_CANDIDATE_EXACT_OBJECT:
		object = candidate->identity;
		if (locklint_object_owner(object) != NULL) {
			(void) fprintf(stream, "%s::",
			    locklint_translation_unit_file(
			    locklint_object_owner(object)));
		}
		(void) fprintf(stream, "%s", locklint_object_name(object));
		if (candidate->role != NULL) {
			symbol = candidate->role->representative;
			if (symbol != NULL && symbol->ident != NULL)
				(void) fprintf(stream, ".%s",
				    show_ident(symbol->ident));
		}
		break;
	case PROTECTION_AUDIT_CANDIDATE_EXACT_STATIC:
		symbol = candidate->identity;
		(void) fprintf(stream, "%s::%s",
		    protection_audit_function_name(candidate->function),
		    symbol->ident != NULL ? show_ident(symbol->ident) :
		    "<anonymous static>");
		break;
	case PROTECTION_AUDIT_CANDIDATE_ROLE:
		protection_audit_print_role(stream, candidate->identity);
		break;
	case PROTECTION_AUDIT_CANDIDATE_UNRESOLVED:
		(void) fprintf(stream, "<unresolved lock");
		if (candidate->function != NULL)
			(void) fprintf(stream, " in %s",
			    protection_audit_function_name(candidate->function));
		(void) fputc('>', stream);
		break;
	}
	if (candidate->offset != 0)
		(void) fprintf(stream, "+%lld", (long long)candidate->offset);
}

static const char *
protection_audit_candidate_kind_name(
    const struct protection_audit_candidate *candidates, size_t count)
{
	unsigned int modes;

	if (count == 0)
		return ("none");
	if (count > 1)
		return ("locks");
	modes = candidates[0].observed_modes;
	if ((modes & LOCKLINT_MODE_MUTEX) != 0 &&
	    (modes & (LOCKLINT_MODE_READER | LOCKLINT_MODE_WRITER)) != 0)
		return ("locks");
	if ((modes & LOCKLINT_MODE_MUTEX) != 0)
		return ("mutex");
	if ((modes & (LOCKLINT_MODE_READER | LOCKLINT_MODE_WRITER)) != 0)
		return ("rwlock");
	return ("unknown");
}

static bool
protection_audit_policy_is_mechanical(
    const struct protection_audit_record *record)
{
	return (record->has_policy &&
	    (record->policy.protection == LOCKLINT_PROTECTION_MUTEX ||
	    record->policy.protection == LOCKLINT_PROTECTION_RWLOCK ||
	    record->policy.protection == LOCKLINT_PROTECTION_LOCK_ROLE));
}

static void
protection_audit_print_protection(FILE *stream,
    const struct protection_audit_record *record,
    const struct protection_audit_candidate *display, size_t display_count)
{
	const char *name = NULL;
	bool read_only_in_name = false;
	bool readable_in_name = false;

	if (record->has_policy) {
		switch (record->policy.protection) {
		case LOCKLINT_PROTECTION_MUTEX:
			name = "mutex";
			break;
		case LOCKLINT_PROTECTION_RWLOCK:
			name = "rwlock";
			break;
		case LOCKLINT_PROTECTION_LOCK_ROLE:
			name = display_count != 0 ?
			    protection_audit_candidate_kind_name(display,
			    display_count) : "lock";
			break;
		case LOCKLINT_PROTECTION_SCHEME:
			name = "external-scheme";
			break;
		case LOCKLINT_PROTECTION_NONE:
			name = record->unresolved ? "unknown" :
			    protection_audit_candidate_kind_name(display,
			    display_count);
			if (strcmp(name, "none") != 0)
				break;
			if (record->policy.read_only) {
				name = "read-only";
				read_only_in_name = true;
			} else if (record->policy.readable_without_lock) {
				name = "readable";
				readable_in_name = true;
			}
			break;
		default:
			abort();
		}
	} else if (record->unresolved) {
		name = "unknown";
	} else {
		name = protection_audit_candidate_kind_name(display,
		    display_count);
	}
	if (name == NULL)
		abort();
	(void) fputs(name, stream);
	if (record->has_policy) {
		if (record->policy.readable_without_lock && !readable_in_name)
			(void) fputs("+readable", stream);
		if (record->policy.read_only && !read_only_in_name)
			(void) fputs("+read-only", stream);
	}
}

static unsigned int
protection_audit_record_notes(const struct protection_audit_record *record)
{
	unsigned int notes = protection_audit_note_flags(record->written,
	    record->candidate_count, record->observed_count,
	    record->unsuitable_mode, record->unresolved);

	if (record->has_policy &&
	    ((!record->written && (record->policy.read_only ||
	    record->policy.readable_without_lock)) ||
	    record->policy.protection == LOCKLINT_PROTECTION_SCHEME))
		notes &= ~PROTECTION_AUDIT_NOTE_NONE;
	if (protection_audit_policy_is_mechanical(record) &&
	    record->candidate_count == 0 && record->observed_count == 0) {
		notes &= ~PROTECTION_AUDIT_NOTE_NONE;
		notes |= PROTECTION_AUDIT_NOTE_INCONSISTENT;
	}
	if (record->has_policy && record->policy.read_only && record->written)
		notes |= PROTECTION_AUDIT_NOTE_POLICY;
	return (notes);
}

static void
protection_audit_print_notes(FILE *stream, unsigned int notes)
{
	static const unsigned int note_bits[] = {
		PROTECTION_AUDIT_NOTE_INCONSISTENT,
		PROTECTION_AUDIT_NOTE_VARYING,
		PROTECTION_AUDIT_NOTE_NONE,
		PROTECTION_AUDIT_NOTE_MODE,
		PROTECTION_AUDIT_NOTE_UNRESOLVED,
		PROTECTION_AUDIT_NOTE_POLICY
	};
	size_t index;
	bool first = true;

	if (notes == 0)
		return;
	(void) fputc('(', stream);
	for (index = 0; index < sizeof (note_bits) / sizeof (note_bits[0]);
	    index++) {
		if ((notes & note_bits[index]) == 0)
			continue;
		(void) fprintf(stream, "%s%zu", first ? "note" : ",",
		    index + 1);
		first = false;
	}
	(void) fputc(')', stream);
}

static int
protection_audit_candidate_report_compare(const void *left_arg,
    const void *right_arg)
{
	const struct protection_audit_candidate *left = left_arg;
	const struct protection_audit_candidate *right = right_arg;
	int comparison;

	if (left->kind < right->kind)
		return (-1);
	if (left->kind > right->kind)
		return (1);
	switch (left->kind) {
	case PROTECTION_AUDIT_CANDIDATE_EXACT_OBJECT: {
		const struct object_identity *left_object = left->identity;
		const struct object_identity *right_object = right->identity;

		comparison = compare_tu(locklint_object_owner(left_object),
		    locklint_object_owner(right_object));
		if (comparison == 0) {
			comparison = compare_string(
			    locklint_object_name(left_object),
			    locklint_object_name(right_object));
		}
		break;
	}
	case PROTECTION_AUDIT_CANDIDATE_EXACT_STATIC: {
		const struct symbol *left_symbol = left->identity;
		const struct symbol *right_symbol = right->identity;

		comparison = compare_tu(left->function != NULL ?
		    left->function->tu : NULL, right->function != NULL ?
		    right->function->tu : NULL);
		if (comparison == 0) {
			comparison = compare_string(
			    protection_audit_function_name(left->function),
			    protection_audit_function_name(right->function));
		}
		if (comparison == 0) {
			comparison = compare_string(
			    left_symbol->ident != NULL ?
			    show_ident(left_symbol->ident) : NULL,
			    right_symbol->ident != NULL ?
			    show_ident(right_symbol->ident) : NULL);
		}
		break;
	}
	case PROTECTION_AUDIT_CANDIDATE_ROLE:
		if (type_report_order(type_member_owner(left->identity)) <
		    type_report_order(type_member_owner(right->identity)))
			comparison = -1;
		else if (type_report_order(type_member_owner(left->identity)) >
		    type_report_order(type_member_owner(right->identity)))
			comparison = 1;
		else if (type_member_report_order(left->identity) <
		    type_member_report_order(right->identity))
			comparison = -1;
		else if (type_member_report_order(left->identity) >
		    type_member_report_order(right->identity))
			comparison = 1;
		else
			comparison = 0;
		break;
	case PROTECTION_AUDIT_CANDIDATE_UNRESOLVED:
		comparison = compare_tu(left->function != NULL ?
		    left->function->tu : NULL, right->function != NULL ?
		    right->function->tu : NULL);
		if (comparison == 0) {
			comparison = compare_string(
			    protection_audit_function_name(left->function),
			    protection_audit_function_name(right->function));
		}
		break;
	default:
		abort();
	}
	if (comparison != 0)
		return (comparison);
	return (left->offset < right->offset ? -1 :
	    left->offset > right->offset ? 1 : 0);
}

static void
protection_audit_result_render_inventory(struct protection_audit_result *result)
{
	struct protection_audit_record **ordered;
	struct protection_audit_record *record;
	FILE *stream = locklint_output_stream(&protection_output);
	size_t count = avl_numnodes(&result->records);
	size_t index = 0;
	unsigned int report_notes = 0;

	if (stream == NULL)
		return;
	ordered = calloc(count, sizeof (*ordered));
	if (ordered == NULL && count != 0)
		die("cannot allocate ordered protection audit records");
	for (record = avl_first(&result->records); record != NULL;
	    record = AVL_NEXT(&result->records, record))
		ordered[index++] = record;
	qsort(ordered, count, sizeof (*ordered),
	    protection_audit_record_report_compare);
	(void) fputs("Protection audit: observations include only analyzed "
	    "roots, resolved targets, and input translation units.\n", stream);
	(void) fputs("DATUM\tACCESS\tPROTECTION\tLOCK\n", stream);
	for (index = 0; index < count; index++) {
		const struct protection_audit_candidate *display;
		struct protection_audit_candidate *ordered_candidates = NULL;
		size_t display_count;
		size_t candidate;
		unsigned int notes;

		record = ordered[index];
		if (record->candidate_count != 0) {
			display = record->candidates;
			display_count = record->candidate_count;
		} else {
			display = record->observed;
			display_count = record->observed_count;
		}
		if (display_count != 0) {
			ordered_candidates = malloc(display_count *
			    sizeof (*ordered_candidates));
			if (ordered_candidates == NULL)
				die("cannot allocate ordered protection "
				    "candidates");
			(void) memcpy(ordered_candidates, display,
			    display_count * sizeof (*ordered_candidates));
			qsort(ordered_candidates, display_count,
			    sizeof (*ordered_candidates),
			    protection_audit_candidate_report_compare);
			display = ordered_candidates;
		}
		notes = protection_audit_record_notes(record);
		report_notes |= notes;
		protection_audit_print_datum(stream, &record->datum);
		(void) fprintf(stream, "\t%s\t",
		    record->read && record->written ? "read/write" :
		    record->written ? "write-only" : "read-only");
		protection_audit_print_protection(stream, record, display,
		    display_count);
		protection_audit_print_notes(stream, notes);
		(void) fputc('\t', stream);
		if (display_count == 0 &&
		    protection_audit_policy_is_mechanical(record)) {
			char *name =
			    locklint_access_name(&record->policy_lock);

			(void) fputs(name, stream);
			free(name);
		} else if (display_count == 0) {
			(void) fputc('-', stream);
		}
		for (candidate = 0; candidate < display_count; candidate++) {
			if (candidate != 0)
				(void) fputs(", ", stream);
			protection_audit_print_candidate(stream,
			    &display[candidate]);
		}
		if (record->candidate_count == 0 &&
		    record->observed_truncated)
			(void) fputs(", ...", stream);
		(void) fputc('\n', stream);
		free(ordered_candidates);
	}
	if (report_notes != 0) {
		(void) fputs("\nNotes:\n", stream);
		if ((report_notes & PROTECTION_AUDIT_NOTE_INCONSISTENT) != 0)
			(void) fputs("note1: lock not held consistently\n",
			    stream);
		if ((report_notes & PROTECTION_AUDIT_NOTE_VARYING) != 0)
			(void) fputs("note2: different locks held\n", stream);
		if ((report_notes & PROTECTION_AUDIT_NOTE_NONE) != 0)
			(void) fputs("note3: no lock consistently held\n",
			    stream);
		if ((report_notes & PROTECTION_AUDIT_NOTE_MODE) != 0)
			(void) fputs("note4: lock mode not consistently "
			    "suitable\n", stream);
		if ((report_notes & PROTECTION_AUDIT_NOTE_UNRESOLVED) != 0)
			(void) fputs("note5: held-lock relationship could not "
			    "be resolved\n", stream);
		if ((report_notes & PROTECTION_AUDIT_NOTE_POLICY) != 0)
			(void) fputs("note6: observed access contradicts "
			    "declared policy\n", stream);
		(void) fputs("\nUse --audit-unprotected for details about "
		    "entries marked with notes.\n", stream);
	}
	free(ordered);
}

static const char *
protection_audit_note_reason(unsigned int notes)
{
	if ((notes & PROTECTION_AUDIT_NOTE_POLICY) != 0)
		return ("observed access contradicts declared policy");
	if ((notes & PROTECTION_AUDIT_NOTE_MODE) != 0)
		return ("lock mode is not consistently suitable");
	if ((notes & PROTECTION_AUDIT_NOTE_UNRESOLVED) != 0)
		return ("held-lock relationship could not be resolved");
	if ((notes & PROTECTION_AUDIT_NOTE_VARYING) != 0)
		return ("different locks are held");
	if ((notes & PROTECTION_AUDIT_NOTE_INCONSISTENT) != 0)
		return ("lock is not held consistently");
	if ((notes & PROTECTION_AUDIT_NOTE_NONE) != 0)
		return ("no lock is consistently held");
	abort();
}

static void
protection_audit_print_site(FILE *stream,
    const struct protection_audit_site *site)
{
	struct position position = protection_audit_site_position(site);
	size_t index;

	(void) fprintf(stream, "    %s:%u in %s(): %s",
	    stream_name(position.stream), position.line,
	    protection_audit_function_name(site->function),
	    site->read && site->written ? "read and written" :
	    site->written ? "written" : "read");
	if (site->observed_count == 0) {
		(void) fputs(site->unsuitable_mode ?
		    " with no suitably held lock" : " with no lock held",
		    stream);
	} else {
		(void) fputs(site->no_lock ?
		    " with varying lock state; observed " :
		    " while holding ", stream);
		for (index = 0; index < site->observed_count; index++) {
			if (index != 0)
				(void) fputs(", ", stream);
			protection_audit_print_candidate(stream,
			    &site->observed[index]);
		}
		if (site->observed_truncated)
			(void) fputs(", ...", stream);
		if (site->unsuitable_mode)
			(void) fputs(" with an unsuitable mode in at least one "
			    "state", stream);
	}
	(void) fputc('\n', stream);
}

static void
protection_audit_result_render_unprotected(
    struct protection_audit_result *result)
{
	struct protection_audit_record **ordered;
	struct protection_audit_record *record;
	FILE *stream = locklint_output_stream(&unprotected_output);
	size_t count = avl_numnodes(&result->records);
	size_t index = 0;

	if (stream == NULL)
		return;
	ordered = calloc(count, sizeof (*ordered));
	if (ordered == NULL && count != 0)
		die("cannot allocate ordered unprotected audit records");
	for (record = avl_first(&result->records); record != NULL;
	    record = AVL_NEXT(&result->records, record))
		ordered[index++] = record;
	qsort(ordered, count, sizeof (*ordered),
	    protection_audit_record_report_compare);
	(void) fputs("Unprotected audit: observations include only analyzed "
	    "roots, resolved targets, and input translation units.\n", stream);
	for (index = 0; index < count; index++) {
		unsigned int notes;
		size_t site;

		record = ordered[index];
		notes = protection_audit_record_notes(record);
		if (notes == 0)
			continue;
		protection_audit_print_datum(stream, &record->datum);
		(void) fprintf(stream, " - %s\n",
		    protection_audit_note_reason(notes));
		qsort(record->sites, record->site_count,
		    sizeof (*record->sites), protection_audit_site_report_compare);
		for (site = 0; site < record->site_count; site++)
			protection_audit_print_site(stream, record->sites[site]);
		if (record->total_sites > record->site_count) {
			(void) fprintf(stream, "    (and %zu more access site%s)\n",
			    record->total_sites - record->site_count,
			    record->total_sites - record->site_count == 1 ?
			    "" : "s");
		}
	}
	free(ordered);
}

void
protection_audit_result_render(struct protection_audit_result *result)
{
	protection_audit_result_render_inventory(result);
	protection_audit_result_render_unprotected(result);
}

void
protection_audit_result_free(struct protection_audit_result *result)
{
	struct protection_audit_record *record;
	void *cookie = NULL;

	if (result == NULL)
		return;
	while ((record = avl_destroy_nodes(&result->records, &cookie)) != NULL) {
		size_t site;

		free(record->candidates);
		for (site = 0; site < record->site_count; site++)
			free(record->sites[site]);
		free(record->sites);
		free(record);
	}
	avl_destroy(&result->records);
	free(result->objects);
	free(result);
}
