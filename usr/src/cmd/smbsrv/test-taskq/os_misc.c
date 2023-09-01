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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2014 Joyent, Inc.  All rights reserved.
 */

/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 * Copyright 2022 Tintri by DDN, Inc. All rights reserved.
 * Copyright 2023 RackTop Systems, Inc.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/taskq_impl.h>
#include <stdio.h>
#include "test_defs.h"

pri_t	minclsyspri = 60;

int boot_max_ncpus = -1;
int max_ncpus = 0;

ulong_t freemem = 100;
ulong_t throttlefree = 20;

#define	IS_DIGIT(c)	((c) >= '0' && (c) <= '9')

#define	IS_ALPHA(c)	\
	(((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))

/*
 * Convert a string into a valid C identifier by replacing invalid
 * characters with '_'.  Also makes sure the string is nul-terminated
 * and takes up at most n bytes.
 */
void
strident_canon(char *s, size_t n)
{
	char c;
	char *end = s + n - 1;

	ASSERT(n > 0);

	if ((c = *s) == 0)
		return;

	if (!IS_ALPHA(c) && c != '_')
		*s = '_';

	while (s < end && ((c = *(++s)) != 0)) {
		if (!IS_ALPHA(c) && !IS_DIGIT(c) && c != '_')
			*s = '_';
	}
	*s = 0;
}

kthread_t *
lwp_kernel_create(proc_t *p, void (*proc)(), void *arg, int state, pri_t pri)
{
	kthread_t	*t;

	t = thread_create(NULL, 0, proc, arg, 0, p, state, pri);
	return (t);
}
