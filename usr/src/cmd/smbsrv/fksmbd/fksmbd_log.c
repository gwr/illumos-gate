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
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 */

#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <smbsrv/libsmb.h>
#include <sys/strlog.h>
#include "smbd.h"

static const char *pri_name[LOG_DEBUG+1] = {
	"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"
};

/*
 * Helper for smb_vsyslog().  Does %m substitutions.
 */
static const char *
format_m(char *buf, const char *str, int err, int buflen)
{
	char		*bp = buf;
	const char	*sp = str;
	const char	*endp = buf + buflen - 1;

	while ((*bp = *sp) != '\0' && bp != endp) {
		if ((*sp++ == '%') && (*sp == 'm')) {
			sp++;
			if (strerror_r(err, bp, endp - bp) == 0)
				bp += strlen(bp);
		} else {
			bp++;
		}
	}
	*bp = '\0';

	return (buf);
}

/*
 * Provide a replacement for libsmb:smb_vsyslog() that just
 * prints the messages to stdout for "fksmbd" debugging.
 */
void
smb_vsyslog(int pri, const char *fmt, va_list ap)
{
	int save_errno = errno;
	char fmtbuf[SMB_LOG_LINE_SZ];

	pri &= LOG_PRIMASK;

	if (smbd.s_debug == 0 && pri > LOG_INFO)
		return;

	(void) fprintf(stdout, "fksmbd: [daemon.%s] ", pri_name[pri]);
	/* LINTED E_SEC_PRINTF_VAR_FMT */
	(void) vfprintf(stdout,
	    format_m(fmtbuf, fmt, save_errno, sizeof (fmtbuf)), ap);
	(void) fprintf(stdout, "\n");
	(void) fflush(stdout);
}

/*
 * Provide a real function (one that prints something) to replace
 * the stub in libfakekernel.  This prints cmn_err() messages.
 */
void
fakekernel_putlog(char *msg, size_t len, int flags)
{

	/*
	 * [CE_CONT, CE_NOTE, CE_WARN, CE_PANIC] maps to
	 * [SL_NOTE, SL_NOTE, SL_WARN, SL_FATAL]
	 */
	if (smbd.s_debug == 0 && (flags & SL_NOTE))
		return;
	(void) fwrite(msg, 1, len, stdout);
	(void) fflush(stdout);
}
