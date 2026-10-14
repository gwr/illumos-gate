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
 * Exercise direct DDI-to-bus forwarding represented by the common model.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/devops.h>
#include <sys/epm.h>
#include <sys/sunddi.h>
#include <sys/sunndi.h>
#include <sys/sunpm.h>

void
check_ddi_forwarding(void)
{
	(void) ddi_dma_mctl(NULL, NULL, NULL, 0, NULL, NULL, NULL, 0);
	(void) ddi_dma_sync(NULL, 0, 0, 0);
	(void) ddi_prop_op(0, NULL, PROP_LEN, 0, NULL, NULL, NULL);
	(void) ddi_get_eventcookie(NULL, NULL, NULL);
	(void) ddi_add_event_handler(NULL, NULL, NULL, NULL, NULL);
	(void) ddi_remove_event_handler(NULL);
	(void) pm_set_power(NULL, 0, 0, 0, 0, 0, NULL);
	(void) pm_raise_power(NULL, 0, 0);
	(void) pm_lower_power(NULL, 0, 0);
	(void) ndi_devi_online(NULL, 0);
	(void) ndi_devi_offline(NULL, 0);
}
