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
 * Measure coarse driver phases with a monotonic clock.  Timing is development
 * output and remains separate from analyzer diagnostics and dump streams.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib.h"
#include "timing.h"

static const char *const phase_names[TIMING_PHASES] = {
	[TIMING_INITIALIZE] = "initialize",
	[TIMING_INPUT_PARSE] = "input-parse",
	[TIMING_INPUT_IDENTITIES] = "input-identities",
	[TIMING_INPUT_COMMAND_NAMES] = "input-command-names",
	[TIMING_INPUT_EVIDENCE] = "input-evidence",
	[TIMING_INPUT_SYMBOLS] = "input-symbols",
	[TIMING_INPUT_CLEANUP] = "input-cleanup",
	[TIMING_COMMANDS] = "commands",
	[TIMING_ANALYSIS_SETUP] = "analysis-setup",
	[TIMING_FIXED_POINT] = "fixed-point",
	[TIMING_DIAG_DECLARED_EFFECTS] = "diag-decl-effects",
	[TIMING_DIAG_LOCK_TRANSITIONS] = "diag-transitions",
	[TIMING_DIAG_DECLARED_ORDER] = "diag-decl-order",
	[TIMING_DIAG_LOCK_ASSERTIONS] = "diag-assertions",
	[TIMING_DIAG_COMPETITION_UNDERFLOW] = "diag-comp-underflow",
	[TIMING_DIAG_COMPETITION_EFFECTS] = "diag-comp-effects",
	[TIMING_DIAG_COMPETITION_ASSERTIONS] = "diag-comp-assert",
	[TIMING_DIAG_PROTECTED_ACCESSES] = "diag-protected",
	[TIMING_DIAG_ASSUMED_CALLS] = "diag-assumed",
	[TIMING_DIAG_LOCKS_ON_RETURN] = "diag-returns",
	[TIMING_DIAG_OBSERVED_ORDER] = "diag-observed-order",
	[TIMING_MEASUREMENT] = "measurement",
	[TIMING_ANALYSIS_CLEANUP] = "analysis-cleanup",
	[TIMING_FINAL_OUTPUT] = "final-output"
};

static struct timespec program_start;
static struct timespec phase_start;
static double phase_elapsed[TIMING_PHASES];
static enum timing_phase active_phase;
static bool enabled;
static bool active;

static void
get_monotonic_time(struct timespec *time)
{
	if (clock_gettime(CLOCK_MONOTONIC, time) != 0)
		die("cannot read monotonic clock: %s", strerror(errno));
}

static double
elapsed_seconds(const struct timespec *start, const struct timespec *end)
{
	time_t seconds = end->tv_sec - start->tv_sec;
	long nanoseconds = end->tv_nsec - start->tv_nsec;

	if (nanoseconds < 0) {
		seconds--;
		nanoseconds += 1000000000L;
	}
	return ((double)seconds + (double)nanoseconds / 1000000000.0);
}

void
timing_start(void)
{
	get_monotonic_time(&program_start);
}

void
timing_enable(void)
{
	enabled = true;
	active = true;
	active_phase = TIMING_INITIALIZE;
	phase_start = program_start;
}

void
timing_begin(enum timing_phase phase)
{
	if (!enabled)
		return;
	if ((unsigned int)phase >= TIMING_PHASES)
		die("invalid timing phase");
	if (active)
		die("timing phase already active");
	get_monotonic_time(&phase_start);
	active_phase = phase;
	active = true;
}

void
timing_end(enum timing_phase phase)
{
	struct timespec now;

	if (!enabled)
		return;
	if (!active || phase != active_phase)
		die("timing phase mismatch");
	get_monotonic_time(&now);
	phase_elapsed[phase] += elapsed_seconds(&phase_start, &now);
	active = false;
}

void
timing_report(FILE *stream)
{
	struct timespec now;
	double total;
	unsigned int phase;

	if (!enabled)
		return;
	if (active)
		die("timing phase still active");
	get_monotonic_time(&now);
	total = elapsed_seconds(&program_start, &now);
	for (phase = 0; phase < TIMING_PHASES; phase++) {
		(void) fprintf(stream, "time %-20s %.3f\n", phase_names[phase],
		    phase_elapsed[phase]);
	}
	(void) fprintf(stream, "time %-20s %.3f\n", "total", total);
}
