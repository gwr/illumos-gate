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

#ifndef PROTECTION_AUDIT_H
#define	PROTECTION_AUDIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lock_identity.h"

struct ll_type;
struct function_info;
struct instruction;
struct locklint_access;
struct locklint_member_path;
struct object_identity;
struct semantic_state;
struct symbol;
struct translation_unit;
struct protection_audit_result;

enum protection_audit_datum_kind {
	PROTECTION_AUDIT_DATUM_EXTERNAL,
	PROTECTION_AUDIT_DATUM_INTERNAL,
	PROTECTION_AUDIT_DATUM_STRUCTURAL,
	PROTECTION_AUDIT_DATUM_LOCAL_STATIC
};

enum protection_audit_key_result {
	PROTECTION_AUDIT_KEY_OK,
	PROTECTION_AUDIT_KEY_EXCLUDED,
	PROTECTION_AUDIT_KEY_UNSUPPORTED
};

enum protection_audit_candidate_kind {
	PROTECTION_AUDIT_CANDIDATE_EXACT_OBJECT,
	PROTECTION_AUDIT_CANDIDATE_EXACT_STATIC,
	PROTECTION_AUDIT_CANDIDATE_ROLE,
	PROTECTION_AUDIT_CANDIDATE_UNRESOLVED
};

enum protection_audit_evidence {
	PROTECTION_AUDIT_EVIDENCE_UNRESOLVED,
	PROTECTION_AUDIT_EVIDENCE_MERGED,
	PROTECTION_AUDIT_EVIDENCE_PROVEN,
	PROTECTION_AUDIT_EVIDENCE_EXACT
};

enum protection_audit_note {
	PROTECTION_AUDIT_NOTE_INCONSISTENT = 1 << 0,
	PROTECTION_AUDIT_NOTE_VARYING = 1 << 1,
	PROTECTION_AUDIT_NOTE_NONE = 1 << 2,
	PROTECTION_AUDIT_NOTE_MODE = 1 << 3,
	PROTECTION_AUDIT_NOTE_UNRESOLVED = 1 << 4,
	PROTECTION_AUDIT_NOTE_POLICY = 1 << 5
};

/*
 * Retain analysis-lifetime identities and numeric region coordinates here.
 * Renderers create display strings only when a selected report is emitted.
 */
struct protection_audit_datum_key {
	enum protection_audit_datum_kind kind;
	struct object_identity *object;
	struct translation_unit *tu;
	const struct function_info *function;
	struct symbol *symbol;
	const struct ll_type *type;
	struct locklint_member_path *path;
	unsigned long offset;
	uint64_t size;
};

/*
 * Candidate identity and evidence strength are separate.  A role remains the
 * same candidate when observations prove different relationship strengths.
 */
struct protection_audit_candidate {
	enum protection_audit_candidate_kind kind;
	enum protection_audit_evidence evidence;
	const void *identity;
	const struct function_info *function;
	const struct type_member *role;
	int64_t offset;
	unsigned int observed_modes;
};

enum protection_audit_key_result protection_audit_datum_key_init(
    struct protection_audit_datum_key *, struct translation_unit *,
    const struct function_info *, const struct locklint_access *);
int protection_audit_datum_identity_compare(
    const struct protection_audit_datum_key *,
    const struct protection_audit_datum_key *);
int protection_audit_datum_report_compare(
    const struct protection_audit_datum_key *,
    const struct protection_audit_datum_key *);
bool protection_audit_candidate_init(struct protection_audit_candidate *,
    const struct function_info *, const struct locklint_access *,
    const void *, int64_t, const struct lock_identity *, bool, bool,
    unsigned int, bool);
int protection_audit_candidate_identity_compare(
    const struct protection_audit_candidate *,
    const struct protection_audit_candidate *);
bool protection_audit_candidate_combine(
    struct protection_audit_candidate *,
    const struct protection_audit_candidate *);
unsigned int protection_audit_note_flags(bool, size_t, size_t, bool, bool);
void protection_audit_output_register(void);
bool protection_audit_option(const char *);
bool protection_audit_is_enabled(void);
void protection_audit_options_validate(void);
struct protection_audit_result *protection_audit_result_create(void);
void protection_audit_result_begin_function(struct protection_audit_result *);
void protection_audit_result_end_function(struct protection_audit_result *);
void protection_audit_result_observe(struct protection_audit_result *,
    const struct function_info *, const struct locklint_access *,
    const struct instruction *, const void *, int64_t,
    const struct semantic_state *, bool);
void protection_audit_result_render(struct protection_audit_result *);
void protection_audit_result_free(struct protection_audit_result *);

#endif /* PROTECTION_AUDIT_H */
