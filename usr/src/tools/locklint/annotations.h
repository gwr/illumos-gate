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

#ifndef ANNOTATIONS_H
#define	ANNOTATIONS_H

#include <stdbool.h>
#include <stdio.h>

struct locklint_access;
struct entrypoint;
struct expression;
struct instruction;
struct position;
struct symbol;
struct symbol_list;
struct translation_unit;

enum locklint_protection {
	LOCKLINT_PROTECTION_NONE,
	LOCKLINT_PROTECTION_MUTEX,
	LOCKLINT_PROTECTION_RWLOCK,
	LOCKLINT_PROTECTION_LOCK_ROLE,
	LOCKLINT_PROTECTION_SCHEME
};

struct locklint_data_policy {
	enum locklint_protection protection;
	bool readable_without_lock;
	bool read_only;
	bool lock_role_match;
};

enum locklint_execution_kind {
	LOCKLINT_EXECUTION_NONE,
	LOCKLINT_EXECUTION_NO_COMPETITION,
	LOCKLINT_EXECUTION_COMPETITION,
	LOCKLINT_EXECUTION_INVISIBLE,
	LOCKLINT_EXECUTION_VISIBLE,
	LOCKLINT_EXECUTION_ASSUME_PROTECTED,
	LOCKLINT_EXECUTION_NO_COMPETITION_EFFECT,
	LOCKLINT_EXECUTION_COMPETITION_EFFECT,
	LOCKLINT_EXECUTION_MUTEX_ACQUIRED_EFFECT,
	LOCKLINT_EXECUTION_READ_ACQUIRED_EFFECT,
	LOCKLINT_EXECUTION_WRITE_ACQUIRED_EFFECT,
	LOCKLINT_EXECUTION_LOCK_RELEASED_EFFECT,
	LOCKLINT_EXECUTION_LOCK_UPGRADED_EFFECT,
	LOCKLINT_EXECUTION_LOCK_DOWNGRADED_EFFECT,
	LOCKLINT_EXECUTION_ASSERT_NO_COMPETITION,
	LOCKLINT_EXECUTION_NOT_REACHED,
	LOCKLINT_EXECUTION_ASSERT_NO_LOCKS_HELD
};

enum locklint_declared_lock_effect {
	LOCKLINT_DECLARED_LOCK_NONE,
	LOCKLINT_DECLARED_MUTEX_ACQUIRED,
	LOCKLINT_DECLARED_READ_ACQUIRED,
	LOCKLINT_DECLARED_WRITE_ACQUIRED,
	LOCKLINT_DECLARED_LOCK_RELEASED,
	LOCKLINT_DECLARED_LOCK_UPGRADED,
	LOCKLINT_DECLARED_LOCK_DOWNGRADED
};

enum locklint_command_result {
	LOCKLINT_COMMAND_OK,
	LOCKLINT_COMMAND_INVALID_NAME,
	LOCKLINT_COMMAND_UNRESOLVED_NAME,
	LOCKLINT_COMMAND_AMBIGUOUS_NAME,
	LOCKLINT_COMMAND_INCONSISTENT_TYPE,
	LOCKLINT_COMMAND_CONFLICT,
	LOCKLINT_COMMAND_SCOPE_MISMATCH,
	LOCKLINT_COMMAND_OWNER_MISMATCH
};

struct locklint_command_origin {
	const char *file;
	unsigned long line;
	unsigned long column;
};

typedef void (*locklint_order_edge_f)(const struct locklint_access *,
    const char *, const struct locklint_access *, const char *,
    const struct position *, const char *, unsigned long, void *);
typedef void (*locklint_visibility_target_f)(const struct locklint_access *,
    const struct expression *, void *);

void locklint_annotations_enable(void);
enum locklint_command_result locklint_declare_readable(size_t,
    const char *const *, const char **, const char *, unsigned long);
enum locklint_command_result locklint_declare_lock_order(size_t,
    const char *const *, const char **, const char *, unsigned long);
enum locklint_command_result locklint_declare_protection(
    enum locklint_protection, const char *, size_t, const char *const *,
    const char **,
    struct locklint_command_origin *, const char *, unsigned long);
enum locklint_command_result locklint_declare_scheme(const char *, size_t,
    const char *const *, const char **, struct locklint_command_origin *,
    const char *, unsigned long);
enum locklint_command_result locklint_declare_lock_role(const char *, size_t,
    const char *const *, const char **, const char *, unsigned long);
bool locklint_get_covering_lock(const struct locklint_access *,
    struct locklint_access *);
bool locklint_lock_covers(const struct locklint_access *,
    const struct locklint_access *);
bool locklint_data_policy(const struct locklint_access *,
    struct locklint_data_policy *, struct locklint_access *);
void locklint_for_each_order_edge(locklint_order_edge_f, void *);
enum locklint_execution_kind locklint_get_execution_annotation(
    const struct instruction *);
bool locklint_for_each_visibility_target(struct translation_unit *,
    const struct instruction *, locklint_visibility_target_f, void *);
bool locklint_for_each_assumed_target(struct translation_unit *,
    const struct instruction *, locklint_visibility_target_f, void *);
bool locklint_get_declared_lock_effect(struct translation_unit *,
    const struct instruction *, enum locklint_declared_lock_effect *,
    struct locklint_access *);
void locklint_process_function_annotations(FILE *, struct translation_unit *,
    struct entrypoint *);
void locklint_resolve_annotations(struct symbol_list *);
void locklint_apply_contract_annotations(void);
void locklint_show_annotations(FILE *);

#endif /* ANNOTATIONS_H */
