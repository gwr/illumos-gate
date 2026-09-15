/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.opensolaris.org/os/licensing.
 */

/*
 * Supply the single-threaded module-entry context and typed no-effect targets
 * that the historical Warlock pseudo-kernel and usbecm.wlcmd provided.  An
 * annotation probe lets the comparison verify that OSLL retained source
 * annotations.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/mutex.h>

struct modinfo;
typedef struct usbecm_state usbecm_state_t;
typedef struct msgb mblk_t;

extern int _init(void);
extern int _fini(void);
extern int _info(struct modinfo *);

struct locklint_usbecm_annotation_probe {
	kmutex_t lock;
	int value;
};

_NOTE(MUTEX_PROTECTS_DATA(locklint_usbecm_annotation_probe::lock,
    locklint_usbecm_annotation_probe::value))

int
locklint_usbecm_ds_init(usbecm_state_t *ecmp)
{
	(void) ecmp;
	return (0);
}

int
locklint_usbecm_ds_fini(usbecm_state_t *ecmp)
{
	(void) ecmp;
	return (0);
}

int
locklint_usbecm_ds_start(usbecm_state_t *ecmp)
{
	(void) ecmp;
	return (0);
}

int
locklint_usbecm_ds_stop(usbecm_state_t *ecmp)
{
	(void) ecmp;
	return (0);
}

int
locklint_usbecm_ds_intr_cb(usbecm_state_t *ecmp, mblk_t *data)
{
	(void) ecmp;
	(void) data;
	return (0);
}

/*
 * OSLL starts main without competition, but does not propagate that initial
 * state through calls into another database.  Make the single-threaded state
 * explicit without changing the net competition state.
 */
int
main(void)
{
	_NOTE(COMPETING_THREADS_NOW)
	_NOTE(NO_COMPETING_THREADS_NOW)
	(void) _init();
	(void) _fini();
	(void) _info((struct modinfo *)0);
	return (0);
}
