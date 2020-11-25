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
 * Copyright 2022 Tintri by DDN, Inc. All Rights Reserved.
 */

#include <libmlrpc.h>
#include <sys/sysmacros.h>
#include <strings.h>
#include <assert.h>

/*
 * Functions that handle creating/processing RPC authentication tokens.
 *
 * The ndr_auth_ops_* functions exist to provide common dtrace probes for
 * the SSP functions that can be easily detected or filtered
 * in the provided smbd* dtrace scripts.
 */

static int
ndr_auth_ops_init(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	if (ctx->auth_ops.nao_init == NULL)
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);
	return (ctx->auth_ops.nao_init(ctx->auth_ctx, mxa));
}

static int
ndr_auth_ops_recv(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	if (ctx->auth_ops.nao_recv == NULL)
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);
	return (ctx->auth_ops.nao_recv(ctx->auth_ctx, mxa));
}

static int
ndr_auth_ops_sign(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	if (ctx->auth_ops.nao_sign == NULL)
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);
	return (ctx->auth_ops.nao_sign(ctx->auth_ctx, mxa));
}

static int
ndr_auth_ops_verify(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	if (ctx->auth_ops.nao_verify == NULL)
		return ((ctx->auth_verify_resp) ?
		    NDR_DRC_FAULT_SEC_SSP_FAILED : NDR_DRC_OK);
	return (ctx->auth_ops.nao_verify(ctx->auth_ctx, mxa,
	    ctx->auth_verify_resp));
}

static int
ndr_auth_ops_accept(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	if (ctx->auth_ops.nao_accept == NULL)
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);
	return (ctx->auth_ops.nao_accept(&ctx->auth_ctx, mxa));
}

static void
ndr_auth_ops_destroy(ndr_auth_ctx_t *ctx)
{
	if (ctx->auth_ops.nao_destroy != NULL)
		ctx->auth_ops.nao_destroy(&ctx->auth_ctx);
}

/*
 * Initializes the sec_trailer (ndr_sec_t).
 * The actual token is allocated and set later (in the SSP).
 */
int
ndr_add_auth_token(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa, unsigned long frag_len)
{
	ndr_stream_t *nds = &mxa->send_nds;
	ndr_sec_t *secp = &mxa->send_auth;

	secp->auth_type = ctx->auth_type;
	secp->auth_level = ctx->auth_level;
	secp->auth_rsvd = 0;

	/*
	 * [MS-RPCE] 2.2.2.12 "Authentication Tokens"
	 * auth_pad_len aligns the packet to 16 bytes.
	 */
	secp->auth_pad_len = P2NPHASE(frag_len, 16);
	if (NDS_PAD_PDU(nds, nds->pdu_scan_offset,
	    secp->auth_pad_len, NULL) == 0)
		return (NDR_DRC_FAULT_SEC_ENCODE_TOO_BIG);

	/* PAD_PDU doesn't adjust scan_offset */
	nds->pdu_scan_offset += secp->auth_pad_len;
	nds->pdu_body_size = frag_len + secp->auth_pad_len - nds->pdu_hdr_size;

	secp->auth_context_id = ctx->auth_context_id;
	return (NDR_DRC_OK);
}

/*
 * Does gss_init_sec_context (or equivalent) and creates
 * the sec_trailer and the auth token.
 *
 * Used during binds (and alter context).
 *
 * Currently, only NETLOGON auth with Integrity protection is implemented.
 */
int
ndr_add_sec_context(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	int rc;

	if (ctx->auth_type == NDR_C_AUTHN_NONE ||
	    ctx->auth_level == NDR_C_AUTHN_LEVEL_NONE)
		return (NDR_DRC_OK);

	if (ctx->auth_type != NDR_C_AUTHN_GSS_NETLOGON) {
		ndo_printf(&mxa->send_nds, NULL, "bad auth_type: %#x",
		    ctx->auth_type);
		return (NDR_DRC_FAULT_SEC_AUTH_TYPE_UNIMPLEMENTED);
	}
	if (ctx->auth_level != NDR_C_AUTHN_LEVEL_PKT_INTEGRITY) {
		ndo_printf(&mxa->send_nds, NULL, "bad auth_level: %#x",
		    ctx->auth_type);
		return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_UNIMPLEMENTED);
	}

	if ((rc = ndr_add_auth_token(ctx, mxa,
	    mxa->send_nds.pdu_scan_offset)) != 0)
		return (rc);

	return (ndr_auth_ops_init(ctx, mxa));
}

/*
 * Does response-side gss_init_sec_context (or equivalent) and validates
 * the sec_trailer and the auth token.
 *
 * Used during bind (and alter context) ACKs.
 */
int
ndr_recv_sec_context(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	ndr_sec_t *bind_secp = &mxa->send_auth;
	ndr_sec_t *ack_secp = &mxa->recv_auth;
	int rc;

	if (ctx->auth_type == NDR_C_AUTHN_NONE ||
	    ctx->auth_level == NDR_C_AUTHN_LEVEL_NONE) {
		if (mxa->recv_hdr.common_hdr.auth_length != 0) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "auth_length is not 0");
			return (NDR_DRC_FAULT_SEC_AUTH_LENGTH_INVALID);
		}
		return (NDR_DRC_OK);
	} else if (mxa->recv_hdr.common_hdr.auth_length == 0) {
		ndo_printf(&mxa->recv_nds, NULL, "auth_length is 0");
		return (NDR_DRC_FAULT_SEC_AUTH_LENGTH_INVALID);
	}

	if (bind_secp->auth_type != ack_secp->auth_type) {
		ndo_printf(&mxa->recv_nds, NULL,
		    "auth_type mismatch: bind %#x ack %#x",
		    bind_secp->auth_type, ack_secp->auth_type);
		return (NDR_DRC_FAULT_SEC_AUTH_TYPE_INVALID);
	}
	if (bind_secp->auth_level != ack_secp->auth_level) {
		ndo_printf(&mxa->recv_nds, NULL,
		    "auth_level mismatch: bind %#x ack %#x",
		    bind_secp->auth_level, ack_secp->auth_level);
		return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_INVALID);
	}

	rc = ndr_auth_ops_recv(ctx, mxa);

	if (!NDR_DRC_IS_FAULT(rc) && rc != NDR_DRC_CONTINUE)
		ctx->auth_complete = B_TRUE;
	else
		ctx->auth_complete = B_FALSE;

	return (rc);
}

/*
 * Does gss_MICEx (or equivalent) and creates
 * the sec_trailer and the auth token.
 *
 * Used upon sending a request (client)/response (server) packet.
 */
int
ndr_add_auth(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa, unsigned long frag_len)
{
	int rc;

	if (ctx->auth_type == NDR_C_AUTHN_NONE ||
	    ctx->auth_level == NDR_C_AUTHN_LEVEL_NONE)
		return (NDR_DRC_OK);

	if (!ctx->auth_complete) {
		ndo_printf(&mxa->recv_nds, NULL, "incomplete auth ctx");
		return (NDR_DRC_FAULT_SEC_AUTH_CTX_INVALID);
	}

	if ((rc = ndr_add_auth_token(ctx, mxa, frag_len)) != 0)
		return (rc);

	return (ndr_auth_ops_sign(ctx, mxa));
}

/*
 * Does gss_VerifyMICEx (or equivalent) and validates
 * the sec_trailer and the auth token.
 *
 * Used upon receiving a request (server)/response (client) packet.
 *
 * If auth_verify_resp is B_FALSE, this doesn't verify responses (but
 * the SSP may still have side-effects).
 */
int
ndr_check_auth(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	ndr_sec_t *secp = &mxa->recv_auth;

	if (ctx->auth_type == NDR_C_AUTHN_NONE ||
	    ctx->auth_level == NDR_C_AUTHN_LEVEL_NONE) {
		if (mxa->recv_hdr.common_hdr.auth_length != 0) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "auth_length is not 0");
			return (NDR_DRC_FAULT_SEC_AUTH_LENGTH_INVALID);
		}
		return (NDR_DRC_OK);
	} else if (mxa->recv_hdr.common_hdr.auth_length == 0) {
		ndo_printf(&mxa->recv_nds, NULL, "auth_length is 0");
		return (NDR_DRC_FAULT_SEC_AUTH_LENGTH_INVALID);
	}

	if (!ctx->auth_complete) {
		ndo_printf(&mxa->recv_nds, NULL, "incomplete auth ctx");
		return (NDR_DRC_FAULT_SEC_AUTH_CTX_INVALID);
	}

	if (ctx->auth_type != secp->auth_type) {
		ndo_printf(&mxa->recv_nds, NULL,
		    "auth_type mismatch: expect %#x recv'd %#x",
		    ctx->auth_type, secp->auth_type);
		return (NDR_DRC_FAULT_SEC_AUTH_TYPE_INVALID);
	}
	if (ctx->auth_level != secp->auth_level) {
		ndo_printf(&mxa->recv_nds, NULL,
		    "auth_level mismatch: expect %#x recv'd %#x",
		    ctx->auth_level, secp->auth_level);
		return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_INVALID);
	}
	return (ndr_auth_ops_verify(ctx, mxa));
}

int
ndr_accept_sec_context(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa)
{
	ndr_auth_ssp_t *auth_ssp;
	ndr_common_header_t *hdr = &mxa->recv_hdr.common_hdr;
	int rc;

	/*
	 * Authentication can only change from 'nothing' to 'something';
	 * The level and type cannot be changed once selected.
	 * Additionally, we only support a single auth context, and so
	 * the context ID must be the same as well.
	 */
	if (hdr->ptype == NDR_PTYPE_ALTER_CONTEXT &&
	    ctx->auth_type != NDR_C_AUTHN_NONE) {
		if (ctx->auth_type != mxa->recv_auth.auth_type) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "auth_type mismatch: expect %#x recv'd %#x",
			    ctx->auth_type, mxa->recv_auth.auth_type);
			return (NDR_DRC_FAULT_SEC_AUTH_TYPE_INVALID);
		}
		if (ctx->auth_level != mxa->recv_auth.auth_level) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "auth_level mismatch: expect %#x recv'd %#x",
			    ctx->auth_level, mxa->recv_auth.auth_level);
			return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_INVALID);
		}
		if (ctx->auth_context_id != mxa->recv_auth.auth_context_id) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "ctxid mismatch: expect %#x recv'd %#x",
			    ctx->auth_context_id,
			    mxa->recv_auth.auth_context_id);
			return (NDR_DRC_FAULT_SEC_AUTH_CTX_INVALID);
		}
	} else {
		ndr_rpc_sec_t use_sec;
		ndr_binding_t *bind;

		switch (ctx->auth_use_sec) {
		case NDR_RPCSEC_USE_SVC:
			bind = mxa->binding;
			if (bind == NULL)
				use_sec = NDR_RPCSEC_USE_NEVER;
			else
				use_sec = bind->service->use_rpc_security;
			break;

		case NDR_RPCSEC_USE_NEVER:
		case NDR_RPCSEC_USE_REQUESTED:
		case NDR_RPCSEC_USE_ALWAYS:
			use_sec = ctx->auth_use_sec;
			break;

		default:
			use_sec = NDR_RPCSEC_USE_NEVER;
			break;
		}

		if (use_sec == NDR_RPCSEC_USE_NEVER)
			return (NDR_DRC_OK);
		if (use_sec == NDR_RPCSEC_USE_ALWAYS &&
		    mxa->recv_auth.auth_type == NDR_C_AUTHN_NONE) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "auth required; rejecting noauth bind",
			    hdr->auth_length);
			return (NDR_DRC_FAULT_SEC_META_INVALID);
		}

		ctx->auth_ctx = NULL;
		ctx->auth_type = mxa->recv_auth.auth_type;
		ctx->auth_level = mxa->recv_auth.auth_level;
		ctx->auth_context_id = mxa->recv_auth.auth_context_id;
	}

	if (ctx->auth_type == NDR_C_AUTHN_NONE)
		return (NDR_DRC_OK);

	auth_ssp = ndr_ssp_handlers[ctx->auth_type];
	if (auth_ssp == NULL) {
		ndo_printf(&mxa->recv_nds, NULL, "unknown auth_type %#x",
		    ctx->auth_type);
		return (NDR_DRC_FAULT_SEC_AUTH_TYPE_UNIMPLEMENTED);
	}
	switch (ctx->auth_level) {
	case NDR_C_AUTHN_LEVEL_PKT_INTEGRITY:
		if ((auth_ssp->ssp_flags & NDR_SSP_SUPPORTS_INTEGRITY) == 0) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "auth_type %#x doesn't support integrity",
			    ctx->auth_type);
			return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_UNIMPLEMENTED);
		}
		break;

	case NDR_C_AUTHN_LEVEL_PKT_PRIVACY:
		if ((auth_ssp->ssp_flags & NDR_SSP_SUPPORTS_PRIVACY) == 0) {
			ndo_printf(&mxa->recv_nds, NULL,
			    "auth_type %#x doesn't support privacy",
			    ctx->auth_type);
			return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_UNIMPLEMENTED);
		}
		break;

	case NDR_C_AUTHN_LEVEL_NONE:
		break;

	default:
		ndo_printf(&mxa->recv_nds, NULL,
		    "auth_level %#x unimplemented",
		    ctx->auth_level);
		return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_UNIMPLEMENTED);
	}

	/* struct copy */
	ctx->auth_ops = auth_ssp->ssp_ops;

	rc = ndr_auth_ops_accept(ctx, mxa);

	if (!NDR_DRC_IS_FAULT(rc)) {
		rc = ndr_add_auth_token(ctx, mxa,
		    mxa->send_hdr.common_hdr.frag_length);
		ctx->auth_complete = (rc != NDR_DRC_CONTINUE);
	} else {
		ctx->auth_complete = B_FALSE;
	}

	return (rc);
}

/*
 * Puts the token acquired during accept_sec_context() into the response.
 * We got the token earlier; just encode the trailer here.
 */
int
ndr_accept_sec_fini(ndr_auth_ctx_t *ctx, ndr_xa_t *mxa, unsigned long frag_len)
{
	if (ctx->auth_type == NDR_C_AUTHN_NONE)
		return (NDR_DRC_OK);

	return (ndr_add_auth_token(ctx, mxa, frag_len));
}

void
ndr_auth_destroy_ctx(ndr_auth_ctx_t *ctx)
{
	if (ctx->auth_type != NDR_C_AUTHN_NONE)
		ndr_auth_ops_destroy(ctx);
}
