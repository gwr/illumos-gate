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

#ifndef TIMING_H
#define	TIMING_H

#include <stdio.h>

enum timing_phase {
	TIMING_INITIALIZE,
	TIMING_INPUT_PARSE,
	TIMING_INPUT_IDENTITIES,
	TIMING_INPUT_COMMAND_NAMES,
	TIMING_INPUT_EVIDENCE,
	TIMING_INPUT_SYMBOLS,
	TIMING_INPUT_CLEANUP,
	TIMING_COMMANDS,
	TIMING_ANALYSIS_SETUP,
	TIMING_FIXED_POINT,
	TIMING_DIAG_DECLARED_EFFECTS,
	TIMING_DIAG_LOCK_TRANSITIONS,
	TIMING_DIAG_DECLARED_ORDER,
	TIMING_DIAG_LOCK_ASSERTIONS,
	TIMING_DIAG_COMPETITION_UNDERFLOW,
	TIMING_DIAG_COMPETITION_EFFECTS,
	TIMING_DIAG_COMPETITION_ASSERTIONS,
	TIMING_DIAG_PROTECTED_ACCESSES,
	TIMING_DIAG_ASSUMED_CALLS,
	TIMING_DIAG_LOCKS_ON_RETURN,
	TIMING_DIAG_OBSERVED_ORDER,
	TIMING_MEASUREMENT,
	TIMING_ANALYSIS_CLEANUP,
	TIMING_FINAL_OUTPUT,
	TIMING_PHASES
};

void timing_start(void);
void timing_enable(void);
void timing_begin(enum timing_phase);
void timing_end(enum timing_phase);
void timing_report(FILE *);

#endif /* TIMING_H */
