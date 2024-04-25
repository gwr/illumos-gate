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
 *
 * Copyright 2024 Hans Rosenfeld
 */

/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved	*/

#include "lint.h"
#include <mtlib.h>
#include <stdarg.h>
#include <errno.h>
#include <thread.h>
#include <synch.h>
#include <values.h>
#include "print.h"
#include <sys/types.h>
#include "libc.h"
#include "mse.h"

/*VARARGS1*/
int
dprintf(int fildes, const char *format, ...)
{
	FILE *file;
	ssize_t count;
	rmutex_t *lk;
	va_list ap;

	file = fdopen(fildes, "w");
	if (file == NULL)
		return (EOF);

	va_start(ap, format);

	_SET_ORIENTATION_BYTE(file);

	count = _ndoprnt(format, ap, file, 0);
	va_end(ap);

	/* check for errors or EOF */
	if (FERROR(file) || count ==  EOF) {
		fdclose(file, NULL);
		return (EOF);
	}

	fdclose(file, NULL);

	/* check for overflow */
	if ((size_t)count > MAXINT) {
		errno = EOVERFLOW;
		return (EOF);
	} else {
		return ((int)count);
	}
}
