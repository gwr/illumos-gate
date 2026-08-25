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

/*
 * Note: Need <ucontex.h> first to avoid stdio pulling in signal
 * before ucontext does that. (necessary for this test)
 */
#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef TEST_ENV
#error "TEST_ENV must name the compilation environment"
#endif

#define	SIGSET_ABI_SIZE	16

int
main(void)
{
	unsigned long sigset_size = sizeof (sigset_t);
	unsigned long member_size =
	    sizeof (((ucontext_t *)0)->uc_sigmask);

	if (sigset_size == SIGSET_ABI_SIZE &&
	    member_size == SIGSET_ABI_SIZE &&
	    sigset_size == member_size) {
		return (EXIT_SUCCESS);
	}

	(void) fprintf(stderr,
	    "%s %lu-bit: expected a %u-byte signal set; "
	    "sizeof (sigset_t) = %lu, "
	    "sizeof (ucontext_t.uc_sigmask) = %lu\n",
	    TEST_ENV, (unsigned long)(sizeof (void *) * 8),
	    SIGSET_ABI_SIZE, sigset_size, member_size);

	return (EXIT_FAILURE);
}
