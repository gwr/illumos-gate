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
 * Supply the single-threaded module-entry context that the historical
 * Warlock pseudo-kernel provided, and an annotation probe that lets the
 * comparison verify that OSLL retained source annotations.
 */

#include <sys/note.h>
#include <sys/mutex.h>

struct modinfo;

extern int _init(void);
extern int _fini(void);
extern int _info(struct modinfo *);

struct locklint_ugen_annotation_probe {
	kmutex_t lock;
	int value;
};

_NOTE(MUTEX_PROTECTS_DATA(locklint_ugen_annotation_probe::lock,
    locklint_ugen_annotation_probe::value))

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
