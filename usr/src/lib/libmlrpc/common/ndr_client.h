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
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 */

#ifndef	_NDR_CLIENT_H
#define	_NDR_CLIENT_H

#include <sys/types.h>
#include <sys/uio.h>

#include <smb/wintypes.h>
#include <libmlrpc/ndr.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Private declarations for ndr_client.c
 */

struct ndr_client {
	/* transport stuff (xa_* members) */
	int (*xa_init)(struct ndr_client *, ndr_xa_t *);
	int (*xa_exchange)(struct ndr_client *, ndr_xa_t *);
	int (*xa_read)(struct ndr_client *, ndr_xa_t *);
	/* XXX Need xa_write too! */
	void (*xa_preserve)(struct ndr_client *, ndr_xa_t *);
	void (*xa_destruct)(struct ndr_client *, ndr_xa_t *);
	void (*xa_release)(struct ndr_client *);
	void			*xa_private;
	int			xa_fd;

	ndr_hdid_t		*handle;
	ndr_binding_t		*binding;
	ndr_binding_t		*binding_list;
	ndr_binding_t		binding_pool[NDR_N_BINDING_POOL];

	boolean_t		nonull;
	boolean_t		heap_preserved;
	ndr_heap_t		*heap;
	ndr_stream_t		*recv_nds;
	ndr_stream_t		*send_nds;

	uint32_t		next_call_id;
	unsigned		next_p_cont_id;
};
typedef struct ndr_client ndr_client_t;

int ndr_clnt_bind(struct ndr_client *, ndr_service_t *, ndr_binding_t **);
int ndr_clnt_call(ndr_binding_t *, int, void *);
void ndr_clnt_free_heap(struct ndr_client *);

#ifdef	__cplusplus
}
#endif

#endif	/* _NDR_CLIENT_H */
