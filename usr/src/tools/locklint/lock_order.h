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

void locklint_order_build(void);
void locklint_order_report_declared_cycles(void);
void locklint_order_cleanup(void);

#endif /* LOCK_ORDER_H */
