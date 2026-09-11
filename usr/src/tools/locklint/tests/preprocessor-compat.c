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
 * Verify native and OSLL-compatible preprocessing select the intended source.
 */

#ifndef __locklint__
#error "__locklint__ is not defined"
#endif

#if __locklint__ != 1
#error "__locklint__ does not equal 1"
#endif

#ifdef __lock_lint
#if __lock_lint != 1
#error "__lock_lint does not equal 1"
#endif
int osll_compatibility_mode;
#else
int native_mode;
#endif
