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
 * Verify that DDI callback registration makes exact callback functions
 * independent competing roots.  Registration deliberately holds the mutex;
 * callback analysis must not inherit that registration-time lock.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/devops.h>
#include <sys/note.h>
#include <sys/sunddi.h>
#include <sys/synch.h>
#include <sys/systm.h>

static kmutex_t callback_lock;
static int callback_data;
static uintptr_t callback_list;
static ddi_dma_attr_t callback_dma_attr;

_NOTE(MUTEX_PROTECTS_DATA(callback_lock, callback_data))

static void
timeout_callback(void *arg)
{
	callback_data++;
}

static uint_t
current_intr_callback(caddr_t arg1, caddr_t arg2)
{
	callback_data++;
	return (DDI_INTR_CLAIMED);
}

static uint_t
legacy_intr_callback(caddr_t arg)
{
	callback_data++;
	return (DDI_INTR_CLAIMED);
}

static int
deferred_callback(caddr_t arg)
{
	callback_data++;
	return (1);
}

static int
dma_callback(caddr_t arg)
{
	callback_data++;
	return (1);
}

void
register_ddi_callbacks(void)
{
	mutex_enter(&callback_lock);
	callback_data++;
	(void) timeout(timeout_callback, NULL, 1);
	(void) ddi_intr_add_handler(NULL, current_intr_callback, NULL, NULL);
	(void) ddi_intr_add_softint(NULL, NULL, 0, current_intr_callback,
	    NULL);
	(void) ddi_add_intr(NULL, 0, NULL, NULL, legacy_intr_callback, NULL);
	(void) ddi_add_softintr(NULL, 0, NULL, NULL, NULL,
	    legacy_intr_callback, NULL);
	ddi_set_callback(deferred_callback, NULL, &callback_list);
	(void) ddi_dma_alloc_handle(NULL, &callback_dma_attr, dma_callback,
	    NULL, NULL);
	mutex_exit(&callback_lock);
}
