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

#include <stdint.h>

struct ll_type;
struct function_info;
struct locklint_access;
struct locklint_member_path;
struct object_identity;
struct symbol;
struct translation_unit;

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

enum protection_audit_key_result protection_audit_datum_key_init(
    struct protection_audit_datum_key *, struct translation_unit *,
    const struct function_info *, const struct locklint_access *);
int protection_audit_datum_identity_compare(
    const struct protection_audit_datum_key *,
    const struct protection_audit_datum_key *);
int protection_audit_datum_report_compare(
    const struct protection_audit_datum_key *,
    const struct protection_audit_datum_key *);

#endif /* PROTECTION_AUDIT_H */
