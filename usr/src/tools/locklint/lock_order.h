/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version 1.0
 * of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2026 Gordon W. Ross
 */

/*
 * Build, query, and release the module-wide lock-order graphs.
 */

#ifndef LOCK_ORDER_H
#define	LOCK_ORDER_H

#include <stdbool.h>

struct locklint_access;
struct lock_identity;
struct locklint_order_violation;
struct position;

void locklint_order_build(void);
void locklint_order_report_declared_cycles(void);
void locklint_order_classify_identity(const struct lock_identity *,
    const struct locklint_access *);
void locklint_order_record_identity_acquisition(const struct lock_identity *,
    const struct locklint_access *, const struct position *);
void locklint_order_record_observed_identities(const struct lock_identity *,
    const struct lock_identity *, const struct position *, bool);
const struct locklint_order_violation *
locklint_order_declared_violation(const struct lock_identity *,
    const struct lock_identity *);
void locklint_order_report_declared_violation(
    const struct locklint_order_violation *, const struct position *, bool);
bool locklint_order_check_declared(const struct locklint_access *,
    const struct locklint_access *, const struct position *, bool);
void locklint_order_record_observed(const struct locklint_access *,
    const struct locklint_access *, const struct position *,
    const struct position *, bool, bool);
void locklint_order_report_observed_cycles(void);
void locklint_order_cleanup(void);

#endif /* LOCK_ORDER_H */
