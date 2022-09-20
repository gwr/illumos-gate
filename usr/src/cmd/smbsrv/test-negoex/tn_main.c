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
 * Copyright 2022 RackTop Systems, Inc.
 */

/*
 * Test & debug program for SPNEGO / NEGOEX
 *
 * Compile
 *	SOURCEDEBUG=yes i386_COPTFLAG='' make install
 *
 * Run under debugger like:
 *	gdb proto/root_i386/usr/lib/smbsrv/test-negoex
 */

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// #include <smbsrv/smb_kproto.h>
// #include <smbsrv/smb_oplock.h>

#include "smbd.h"
#include "smbd_authsvc.h"

smbd_t smbd = { .s_debug = 1 };

uchar_t inbuf[] = {
	'\x60', '\x82', '\x01', '\x6d', '\x06', '\x06',
	'\x2b', '\x06',	'\x01', '\x05', '\x05', '\x02', '\xa0', '\x82',
	'\x01', '\x61', '\x30', '\x82', '\x01', '\x5d', '\xa0', '\x1a',
	'\x30', '\x18', '\x06', '\x0a', '\x2b', '\x06', '\x01', '\x04',
	'\x01', '\x82', '\x37', '\x02', '\x02', '\x1e', '\x06', '\x0a',
	'\x2b', '\x06', '\x01', '\x04', '\x01', '\x82', '\x37', '\x02',
	'\x02', '\x0a', '\xa2', '\x82', '\x01', '\x3d', '\x04', '\x82',
	'\x01', '\x39', '\x4e', '\x45', '\x47', '\x4f', '\x45', '\x58',
	'\x54', '\x53', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x60', '\x00', '\x00', '\x00', '\x70', '\x00',
	'\x00', '\x00', '\xbc', '\x68', '\x16', '\xa4', '\xa6', '\x0c',
	'\xaa', '\xf5', '\x30', '\x15', '\xa0', '\xf6', '\x76', '\x5b',
	'\x16', '\x48', '\x1f', '\xb1', '\x21', '\xea', '\x3f', '\xae',
	'\x4e', '\x9f', '\xb9', '\x67', '\xa3', '\xd6', '\x8a', '\x11',
	'\xb5', '\xc1', '\x02', '\x80', '\x50', '\xdb', '\x76', '\x44',
	'\xdd', '\x0a', '\x9b', '\xa0', '\x86', '\x47', '\x7f', '\x60',
	'\x3d', '\x96', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x60', '\x00', '\x00', '\x00', '\x01', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x5c', '\x33', '\x53', '\x0d', '\xea', '\xf9',
	'\x0d', '\x4d', '\xb2', '\xec', '\x4a', '\xe3', '\x78', '\x6e',
	'\xc3', '\x08', '\x4e', '\x45', '\x47', '\x4f', '\x45', '\x58',
	'\x54', '\x53', '\x02', '\x00', '\x00', '\x00', '\x01', '\x00',
	'\x00', '\x00', '\x40', '\x00', '\x00', '\x00', '\xc9', '\x00',
	'\x00', '\x00', '\xbc', '\x68', '\x16', '\xa4', '\xa6', '\x0c',
	'\xaa', '\xf5', '\x30', '\x15', '\xa0', '\xf6', '\x76', '\x5b',
	'\x16', '\x48', '\x5c', '\x33', '\x53', '\x0d', '\xea', '\xf9',
	'\x0d', '\x4d', '\xb2', '\xec', '\x4a', '\xe3', '\x78', '\x6e',
	'\xc3', '\x08', '\x40', '\x00', '\x00', '\x00', '\x89', '\x00',
	'\x00', '\x00', '\x30', '\x81', '\x86', '\xa0', '\x55', '\x30',
	'\x53', '\x30', '\x51', '\x80', '\x4f', '\x30', '\x4d', '\x31',
	'\x4b', '\x30', '\x49', '\x06', '\x03', '\x55', '\x04', '\x03',
	'\x1e', '\x42', '\x00', '\x4d', '\x00', '\x53', '\x00', '\x2d',
	'\x00', '\x4f', '\x00', '\x72', '\x00', '\x67', '\x00', '\x61',
	'\x00', '\x6e', '\x00', '\x69', '\x00', '\x7a', '\x00', '\x61',
	'\x00', '\x74', '\x00', '\x69', '\x00', '\x6f', '\x00', '\x6e',
	'\x00', '\x2d', '\x00', '\x50', '\x00', '\x32', '\x00', '\x50',
	'\x00', '\x2d', '\x00', '\x41', '\x00', '\x63', '\x00', '\x63',
	'\x00', '\x65', '\x00', '\x73', '\x00', '\x73', '\x00', '\x20',
	'\x00', '\x5b', '\x00', '\x32', '\x00', '\x30', '\x00', '\x32',
	'\x00', '\x32', '\x00', '\x5d', '\xa1', '\x2d', '\x30', '\x2b',
	'\xa0', '\x11', '\x1b', '\x0f', '\x57', '\x45', '\x4c', '\x4c',
	'\x4b', '\x4e', '\x4f', '\x57', '\x4e', '\x3a', '\x50', '\x4b',
	'\x55', '\x32', '\x55', '\xa1', '\x16', '\x30', '\x14', '\xa0',
	'\x03', '\x02', '\x01', '\x80', '\xa1', '\x0d', '\x30', '\x0b',
	'\x1b', '\x09', '\x74', '\x72', '\x61', '\x63', '\x69', '\x66',
	'\x73', '\x30', '\x32'
};

void
do_negoex1(void)
{
	int rc;
	authsvc_context_t *ctx = smbd_authctx_create();

	ctx->ctx_irawtype = LSA_MTYPE_ESFIRST;
	ctx->ctx_irawlen = sizeof (inbuf);
	memcpy(ctx->ctx_irawbuf, inbuf, sizeof (inbuf));

	/*
	 * The real work happens here.
	 */
	rc = smbd_authsvc_dispatch(ctx);
	printf("rc=%d\n", rc);

}

int
main(int argc, char *argv[])
{
	do_negoex1();
	return (0);
}


/*
 * A few functions called by the include code.
 * Stubbed out, and/or just print a message.
 */

int
smb_config_get_secmode()
{
	return (SMB_SECMODE_WORKGRP);
}

int
smbd_krb5ssp_init(authsvc_context_t *ctx)
{
	return (NT_STATUS_INTERNAL_ERROR);
}
void
smbd_krb5ssp_fini(authsvc_context_t *ctx)
{
}
int
smbd_krb5ssp_work(authsvc_context_t *ctx)
{
	return (NT_STATUS_INTERNAL_ERROR);
}


void
smbd_report(const char *fmt, ...)
{
	char buf[128];
	va_list ap;

	if (fmt == NULL)
		return;

	va_start(ap, fmt);
	(void) vsnprintf(buf, 128, fmt, ap);
	va_end(ap);

	(void) fprintf(stderr, "smbd: %s\n", buf);
}

/*
 * Once we're out of memory, we're not likely to recover
 * without a restart. Let SMF restart this service.
 */
void
smbd_nomem(void)
{
	smbd_report(strerror(ENOMEM));
	if (smbd.s_debug)
		abort();
	exit(1);
}

smb_token_t *
smbd_user_auth_logon(smb_logon_t *user_info)
{
	return (NULL);
}

void
smb_token_destroy(smb_token_t *token)
{
}
