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
 * Verify that the representative delay() preserves its current blocking
 * behavior and caller lock state.
 */

#include <sys/types.h>
#include <sys/synch.h>
#include <sys/systm.h>

static kmutex_t delay_outer_lock;

void
check_ddi_delay(void)
{
	mutex_enter(&delay_outer_lock);
	delay(1);
	mutex_exit(&delay_outer_lock);
}
