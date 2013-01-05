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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * Client NDR RPC interface.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <time.h>
#include <strings.h>
#include <assert.h>
#include <errno.h>
#include <thread.h>
#include <syslog.h>
#include <synch.h>

#include <netsmb/smbfs_api.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libmlrpc.h>
#include <smbsrv/libmlsvc.h>
#include <smbsrv/ndl/srvsvc.ndl>
#include <libsmbrdr.h>
#include <mlsvc.h>


/*
 * This call must be made to initialize an RPC client structure and bind
 * to the remote service before any RPCs can be exchanged with that service.
 *
 * The mlsvc_handle_t is a wrapper that is used to associate an RPC handle
 * with the client context for an instance of the interface.  The handle
 * is zeroed to ensure that it doesn't look like a valid handle -
 * handle content is provided by the remove service.
 *
 * The client points to this top-level handle so that we know when to
 * unbind and teardown the connection.  As each handle is initialized it
 * will inherit a reference to the client context.
 *
 * Returns 0 or an NT_STATUS:
 *	NT_STATUS_BAD_NETWORK_PATH	(get server addr)
 *	NT_STATUS_NETWORK_ACCESS_DENIED	(connect, auth)
 *	NT_STATUS_BAD_NETWORK_NAME	(tcon, open)
 *	NT_STATUS_ACCESS_DENIED		(open pipe)
 *	NT_STATUS_INVALID_PARAMETER	(rpc bind)
 *
 *	NT_STATUS_INTERNAL_ERROR	(bad args etc)
 *	NT_STATUS_NO_MEMORY
 */
DWORD
ndr_rpc_bind(mlsvc_handle_t *handle, char *server, char *domain,
    char *username, const char *service)
{
	struct smb_ctx		*ctx = NULL;
	ndr_client_t		*clnt = NULL;
	ndr_service_t		*svc;
	DWORD			status;
	int			fd = -1;
	int			rc;

	if (handle == NULL || server == NULL || server[0] == '\0' ||
	    domain == NULL || username == NULL)
		return (NT_STATUS_INTERNAL_ERROR);

	/* In case the service was not registered... */
	if ((svc = ndr_svc_lookup_name(service)) == NULL)
		return (NT_STATUS_INTERNAL_ERROR);

	/*
	 * Some callers pass this when they want a NULL session.
	 * Todo: have callers pass an empty string for that.
	 */
	if (strcmp(username, MLSVC_ANON_USER) == 0)
		username = "";

	/*
	 * Setup smbfs library handle, authenticate, connect to
	 * the IPC$ share.  This will reuse an existing connection
	 * if the driver already has one for this combination of
	 * server, user, domain.  It may return any of:
	 *	NT_STATUS_BAD_NETWORK_PATH	(get server addr)
	 *	NT_STATUS_NETWORK_ACCESS_DENIED	(connect, auth)
	 *	NT_STATUS_BAD_NETWORK_NAME	(tcon)
	 */
	status = smbrdr_ctx_new(&ctx, server, domain, username);
	if (status != NT_STATUS_SUCCESS) {
		syslog(LOG_ERR, "ndr_rpc_bind: smbrdr_ctx_new"
		    "(Srv=%s Dom=%s User=%s), %s (0x%x)",
		    server, domain, username,
		    xlate_nt_status(status), status);
		/* Tell the DC Locator this DC failed. */
		smb_ddiscover_bad_dc(server);
		goto errout;
	}

	/*
	 * Open the named pipe.
	 */
	fd = smb_fh_open(ctx, svc->endpoint, O_RDWR);
	if (fd < 0) {
		rc = errno;
		syslog(LOG_DEBUG, "ndr_rpc_bind: "
		    "smb_fh_open (%s) err=%d",
		    svc->endpoint, rc);
		switch (rc) {
		case EACCES:
			status = NT_STATUS_ACCESS_DENIED;
			break;
		default:
			status = NT_STATUS_BAD_NETWORK_NAME;
			break;
		}
		goto errout;
	}

	/*
	 * Setup the RPC client handle.
	 */
	if ((clnt = malloc(sizeof (ndr_client_t))) == NULL) {
		status = NT_STATUS_NO_MEMORY;
		goto errout;
	}
	bzero(clnt, sizeof (ndr_client_t));

	clnt->handle = &handle->handle;
	clnt->xa_init = ndr_xa_init;
	clnt->xa_exchange = ndr_xa_exchange;
	clnt->xa_read = ndr_xa_read;
	clnt->xa_preserve = ndr_xa_preserve;
	clnt->xa_destruct = ndr_xa_destruct;
	clnt->xa_release = ndr_xa_release;
	clnt->xa_private = ctx;
	clnt->xa_fd = fd;

	ndr_svc_binding_pool_init(&clnt->binding_list,
	    clnt->binding_pool, NDR_N_BINDING_POOL);

	if ((clnt->heap = ndr_heap_create()) == NULL) {
		status = NT_STATUS_NO_MEMORY;
		goto errout;
	}

	/*
	 * Fill in the caller's handle.
	 */
	bzero(&handle->handle, sizeof (ndr_hdid_t));
	handle->clnt = clnt;

	/*
	 * Do the OtW RPC bind.
	 */
	rc = ndr_clnt_bind(clnt, service, &clnt->binding);
	switch (rc) {
	case NDR_DRC_FAULT_OUT_OF_MEMORY:
		status = NT_STATUS_NO_MEMORY;
		break;
	case NDR_DRC_FAULT_API_SERVICE_INVALID:	/* not registered */
		status = NT_STATUS_INTERNAL_ERROR;
		break;
	default:
		if (NDR_DRC_IS_FAULT(rc)) {
			status = NT_STATUS_INVALID_PARAMETER;
			break;
		}
		/* FALLTHROUGH */
	case NDR_DRC_OK:
		return (NT_STATUS_SUCCESS);
	}

	syslog(LOG_DEBUG, "ndr_rpc_bind: "
	    "ndr_clnt_bind, %s (0x%x)",
	    xlate_nt_status(status), status);

errout:
	handle->clnt = NULL;
	if (clnt != NULL) {
		ndr_heap_destroy(clnt->heap);
		free(clnt);
	}
	if (ctx != NULL) {
		if (fd != -1)
			(void) smb_fh_close(fd);
		smbrdr_ctx_free(ctx);
	}

	return (status);
}

/*
 * Unbind and close the pipe to an RPC service.
 *
 * If the heap has been preserved we need to go through an xa release.
 * The heap is preserved during an RPC call because that's where data
 * returned from the server is stored.
 *
 * Otherwise we destroy the heap directly.
 */
void
ndr_rpc_unbind(mlsvc_handle_t *handle)
{
	ndr_client_t *clnt = handle->clnt;
	struct smb_ctx *ctx = clnt->xa_private;

	if (clnt->heap_preserved)
		ndr_clnt_free_heap(clnt);
	else
		ndr_heap_destroy(clnt->heap);

	(void) smb_fh_close(clnt->xa_fd);
	smbrdr_ctx_free(ctx);
	free(clnt);
	bzero(handle, sizeof (mlsvc_handle_t));
}

void
ndr_rpc_status(mlsvc_handle_t *handle, int opnum, DWORD status)
{
	ndr_service_t *svc;
	char *name = "NDR RPC";
	char *s = "unknown";

	switch (NT_SC_SEVERITY(status)) {
	case NT_STATUS_SEVERITY_SUCCESS:
		s = "success";
		break;
	case NT_STATUS_SEVERITY_INFORMATIONAL:
		s = "info";
		break;
	case NT_STATUS_SEVERITY_WARNING:
		s = "warning";
		break;
	case NT_STATUS_SEVERITY_ERROR:
		s = "error";
		break;
	}

	if (handle) {
		svc = handle->clnt->binding->service;
		name = svc->name;
	}

	smb_tracef("%s[0x%02x]: %s: %s (0x%08x)",
	    name, opnum, s, xlate_nt_status(status), status);
}
