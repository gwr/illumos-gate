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

#ifndef IDENTITY_H
#define	IDENTITY_H

struct object_identity;
struct symbol;
struct symbol_list;
struct translation_unit;

struct translation_unit *locklint_translation_unit_begin(const char *);
struct translation_unit *locklint_translation_unit_current(void);
unsigned int locklint_translation_unit_id(const struct translation_unit *);
const char *locklint_translation_unit_file(const struct translation_unit *);
void locklint_translation_unit_register(struct translation_unit *,
    struct symbol_list *);
bool locklint_symbol_can_use_internal(const struct symbol *);
struct object_identity *locklint_object_identity(struct translation_unit *,
    struct symbol *);
struct object_identity *locklint_external_object(const char *,
    struct symbol **);

#endif /* IDENTITY_H */
