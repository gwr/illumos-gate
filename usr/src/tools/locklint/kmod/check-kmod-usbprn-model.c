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
 * Model the synchronous callbacks made by physio() so locklint can propagate
 * the caller's state through function-valued arguments.  This model is
 * intentionally limited to behavior needed by the usbprn analysis.
 */

#include <sys/types.h>

struct buf;
struct uio;

/*
 * Invoke the size-limiting callback before submitting the buffer, matching
 * the callback order relevant to usbprn.
 */
int
physio(int (*strategy)(struct buf *), struct buf *bp, dev_t dev, int rw,
    void (*mincnt)(struct buf *), struct uio *uio)
{
	(void) dev;
	(void) rw;
	(void) uio;
	mincnt(bp);
	(void) strategy(bp);
	return (0);
}
