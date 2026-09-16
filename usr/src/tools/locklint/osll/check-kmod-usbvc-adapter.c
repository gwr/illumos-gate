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
 * Supply the single-threaded module-entry context and physio callback edges
 * that the historical Warlock pseudo-kernel provided.  An annotation probe
 * lets the comparison verify that OSLL retained source annotations.
 */

#include <sys/types.h>
#include <sys/note.h>
#include <sys/mutex.h>

struct modinfo;
struct buf;
struct uio;

extern int _init(void);
extern int _fini(void);
extern int _info(struct modinfo *);

struct locklint_usbvc_annotation_probe {
	kmutex_t lock;
	int value;
};

_NOTE(MUTEX_PROTECTS_DATA(locklint_usbvc_annotation_probe::lock,
    locklint_usbvc_annotation_probe::value))

/*
 * Preserve the callback edges modeled by the historical ddi_dki_comm.inc.
 */
int
physio(int (*strategy)(struct buf *), struct buf *bp, dev_t dev, int rw,
    void (*mincnt)(struct buf *), struct uio *uio)
{
	(void) dev;
	(void) rw;
	(void) uio;
	(void) strategy(bp);
	mincnt(bp);
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
