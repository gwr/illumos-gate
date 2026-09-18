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

#ifndef ANALYSIS_H
#define	ANALYSIS_H

#include <stdio.h>

/*
 * Run the context analysis over an already resolved callgraph and write its
 * development instrumentation to the requested stream.
 */
void analysis_run(FILE *);

#endif /* ANALYSIS_H */
