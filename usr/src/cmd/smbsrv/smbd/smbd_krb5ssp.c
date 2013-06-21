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

/*
 * Todo: Add Kerberos support.  (just stubs here for now)
 */

#include <sys/types.h>
#include "smbd_authsvc.h"


/*
 * Initialize this context for Kerberos, if possible.
 *
 * Should not get here unless libsmb smb_config_get_negtok
 * includes the Kerberos5 Mech OIDs in our spnego hint.
 *
 * Todo: allocate ctx->ctx_backend
 */
int
smbd_krb5ssp_init(authsvc_context_t *ctx)
{
	_NOTE(ARGUNUSED(ctx))
	return (NT_STATUS_NOT_IMPLEMENTED);
}

/*
 * Todo: free ctx->ctx_backend
 */
void
smbd_krb5ssp_fini(authsvc_context_t *ctx)
{
	_NOTE(ARGUNUSED(ctx))
}

/*
 * Handle a Kerberos auth message.
 *
 * State across messages is in ctx->ctx_backend
 */
int
smbd_krb5ssp_work(authsvc_context_t *ctx)
{
	_NOTE(ARGUNUSED(ctx))
	return (NT_STATUS_NOT_IMPLEMENTED);
}
