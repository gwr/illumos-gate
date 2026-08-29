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

#ifndef CHECK_H
#define	CHECK_H

#include <stdbool.h>

struct entrypoint;
struct symbol_list;
struct translation_unit;

void locklint_check_record_pointer_evidence(struct translation_unit *,
    struct symbol_list *, bool);
void locklint_check_add(struct translation_unit *, struct entrypoint *);
void locklint_check_all(bool, bool);

#endif /* CHECK_H */
