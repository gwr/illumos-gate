/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/varargs.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>

#include <fakekernel.h>
/* Can't use <stdio.h> - conflicts */

char *volatile panicstr;
va_list  panicargs;

int	fprintf(__FILE_TAG *, const char *, ...);
int	vfprintf(__FILE_TAG *, const char *, __va_list);
void	abort(void) __NORETURN;

static const char
ce_prefix[CE_IGNORE][10] = { "", "NOTICE: ", "WARNING: ", "" };

static const char
ce_suffix[CE_IGNORE][2] = { "", "\n", "\n", "" };

volatile int aok;

/*
 * Only print if the main program calls
 * _set_logfp()
 */
static __FILE_TAG *cmn_err_logfp = NULL;
static int cmn_err_level = CE_WARN;

void
fakekernel_redirect_cmn_err(__FILE_TAG *fp, int level)
{
	cmn_err_logfp = fp;
	cmn_err_level = level;
}

void
vpanic(const char *fmt, va_list adx)
{
	__FILE_TAG *fp;

	panicstr = (char *)fmt;
	va_copy(panicargs, adx);

	if ((fp = cmn_err_logfp) != NULL) {
		(void) fprintf(fp, "error: ");
		(void) vfprintf(fp, fmt, adx);
		(void) fprintf(fp, "\n");
	}

	abort();	/* think of it as a "user-level crash dump" */
}

void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vpanic(fmt, adx);
	va_end(adx);
}

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	__FILE_TAG *fp;

	if (ce == CE_PANIC)
		vpanic(fmt, adx);
	/* suppress noise in userland stress testing */
	if (ce < cmn_err_level)
		return;
	if ((fp = cmn_err_logfp) != NULL) {
		(void) fprintf(fp, "%s", ce_prefix[ce]);
		(void) vfprintf(fp, fmt, adx);
		(void) fprintf(fp, "%s", ce_suffix[ce]);
	}
}

/*PRINTFLIKE2*/
void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
}
