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

#ifndef TYPE_H
#define	TYPE_H

#include <stdbool.h>
#include <stdio.h>

struct ident;
struct symbol;
struct symbol_list;

typedef bool (*type_name_visit_f)(struct symbol *, void *);

void type_registry_create(void);
void type_registry_destroy(void);
struct symbol *type_node_strip(struct symbol *);
struct symbol *type_compound_resolve(struct symbol *);
void type_symbols_register(struct symbol_list *);
void type_name_visit(struct ident *, type_name_visit_f, void *);
void type_registry_show(FILE *);

#endif /* TYPE_H */
