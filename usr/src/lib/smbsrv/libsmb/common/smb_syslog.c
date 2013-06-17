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
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <smbsrv/libsmb.h>

#define	SMB_DEFAULT_IDENT "smb"

static const char *pri_name[LOG_DEBUG+1] = {
	"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"
};

static FILE *smb_logfp = NULL;
static const char *smb_logident = SMB_DEFAULT_IDENT;
static int smb_logpri = LOG_INFO;
static void (*_openlog)(const char *, int, int) = NULL;
static void (*_closelog)(void) = NULL;
static void (*_vsyslog)(int, const char *, va_list) = NULL;

#pragma init(_init)

static void
_init(void)
{
	_openlog =
	    (void (*)(const char *, int, int))dlsym(RTLD_NEXT, "openlog");
	_vsyslog =
	    (void (*)(int, const char *, va_list))dlsym(RTLD_NEXT, "vsyslog");
	_closelog = (void (*)(void))dlsym(RTLD_NEXT, "closelog");
}

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
 * Enables syslog redirection to fp, if fp is non-NULL.
 */
void
libsmb_redirect_syslog(FILE *fp, int priority)
{
	smb_logfp = fp;
	smb_logpri = priority & LOG_PRIMASK;
}

/*
 * syslog(3C) interposers for smb.
 */
void
openlog(const char *ident, int logopt, int facility)
{
	assert(ident != NULL);
	smb_logident = ident;
	_openlog(ident, logopt, facility);
}

void
closelog(void)
{
	smb_logident = SMB_DEFAULT_IDENT;
	_closelog();
}

void
vsyslog(int pri, const char *fmt, va_list ap)
{
	int save_errno = errno;
	char fmtbuf[SMB_LOG_LINE_SZ];

	if (smb_logfp == NULL) {
		_vsyslog(pri, fmt, ap);
		return;
	}

	pri &= LOG_PRIMASK;
	if (pri > smb_logpri)
		return;

	(void) fprintf(smb_logfp,
	    "%s: [daemon.%s] ", smb_logident, pri_name[pri]);
	/* LINTED E_SEC_PRINTF_VAR_FMT */
	(void) vfprintf(smb_logfp,
	    format_m(fmtbuf, fmt, save_errno, sizeof (fmtbuf)), ap);
	(void) fprintf(smb_logfp, "\n");
	(void) fflush(smb_logfp);
}

/*PRINTFLIKE2*/
void
syslog(int pri, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(pri, fmt, ap);
	va_end(ap);
}
