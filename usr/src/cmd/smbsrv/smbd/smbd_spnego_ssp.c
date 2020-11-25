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
 * Copyright 2020 Tintri by DDN, Inc. All rights reserved.
 */

/*
 * This implements the SPNEGO SSP for MSRPC
 */

#include <sys/types.h>
#include <strings.h>
#include <limits.h>
#include <gssapi/gssapi_ext.h>
#include <smbsrv/libmlsvc.h>
#include "smbd.h"

static void
smbd_spnego_report_err(const char *funcstr, OM_uint32 major, OM_uint32 minor)
{
	gss_buffer_desc errstr = {0};
	OM_uint32 disp_major, disp_minor, msg_ctx = 0;

	smbd_report("%s: GSSAPI failed; major=0x%x minor=0x%x",
	    funcstr, major, minor);

	do {
		disp_major = gss_display_status(&disp_minor, major,
		    GSS_C_GSS_CODE, GSS_C_NULL_OID, &msg_ctx, &errstr);
		if (!GSS_ERROR(disp_major) && errstr.length != 0) {
			smbd_report("%s: %s", funcstr, errstr.value);
			(void) gss_release_buffer(&disp_minor, &errstr);
		} else if (GSS_ERROR(disp_major)) {
			smbd_report("%s: gss_display_status failed; "
			    "major=0x%x minor=0x%x", funcstr, disp_major,
			    disp_minor);
		}
	} while (!GSS_ERROR(disp_major) && msg_ctx != 0);

	if (minor == 0)
		return;

	do {
		disp_major = gss_display_status(&disp_minor, minor,
		    GSS_C_MECH_CODE, GSS_C_NULL_OID, &msg_ctx, &errstr);
		if (!GSS_ERROR(disp_major) && errstr.length != 0) {
			smbd_report("%s: %s", funcstr, errstr.value);
			(void) gss_release_buffer(&disp_minor, &errstr);
		} else if (GSS_ERROR(disp_major)) {
			smbd_report("%s: gss_display_status failed; "
			    "major=0x%x minor=0x%x", funcstr, disp_major,
			    disp_minor);
		}
	} while (!GSS_ERROR(disp_major) && msg_ctx != 0);
}

/*
 * Copy a token into a newly allocated NDR-managed buffer.
 * Always calls gss_release_buffer() on the token if it's non-NULL.
 */
static boolean_t
smbd_spnego_token_to_ndr(ndr_heap_t *heap, gss_buffer_t gss_tok,
    void **out_buf, uint16_t *out_size)
{
	uint32_t minor;

	if (gss_tok->length == 0)
		return (B_TRUE);

	if (gss_tok->length > USHRT_MAX ||
	    (*out_buf = ndr_heap_malloc(heap, gss_tok->length)) == NULL) {
		(void) gss_release_buffer(&minor, gss_tok);
		return (B_FALSE);
	}

	bcopy(gss_tok->value, *out_buf, gss_tok->length);
	*out_size = gss_tok->length;
	(void) gss_release_buffer(&minor, gss_tok);

	return (B_TRUE);
}

int
smbd_spnego_accept_sec_ctx(void **auth_ctx, ndr_xa_t *mxa)
{
	gss_buffer_desc gss_in_tok = {0}, gss_out_tok = {0};
	gss_ctx_id_t *gss_ctx = (gss_ctx_id_t *)auth_ctx;
	void **out_tok = (void **)&mxa->send_auth.auth_value;
	void *in_tok = mxa->recv_auth.auth_value;
	uint16_t *out_size = &mxa->send_hdr.common_hdr.auth_length;
	uint32_t major, minor;
	OM_uint32 ret_flags;
	int rc = NDR_DRC_OK;
	uint16_t in_size = mxa->recv_hdr.common_hdr.auth_length;
	uint8_t auth_level = mxa->svc_auth_ctx->auth_level;

	if (*auth_ctx == NULL)
		*gss_ctx = GSS_C_NO_CONTEXT;

	*out_tok = NULL;
	*out_size = 0;

	gss_in_tok.value = in_tok;
	gss_in_tok.length = (size_t)in_size;

	major = gss_accept_sec_context(&minor, gss_ctx, GSS_C_NO_CREDENTIAL,
	    &gss_in_tok, GSS_C_NO_CHANNEL_BINDINGS, NULL, NULL,
	    &gss_out_tok, &ret_flags, NULL, NULL);

	if (GSS_ERROR(major)) {
		smbd_spnego_report_err(__func__, major, minor);
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);
	}

	if (major == GSS_S_COMPLETE) {
		if ((auth_level == NDR_C_AUTHN_LEVEL_PKT_INTEGRITY &&
		    (ret_flags & GSS_C_INTEG_FLAG) == 0) ||
		    (auth_level == NDR_C_AUTHN_LEVEL_PKT_PRIVACY &&
		    (ret_flags & GSS_C_CONF_FLAG) == 0)) {
			(void) gss_release_buffer(&minor, &gss_out_tok);
			return (NDR_DRC_FAULT_SEC_AUTH_LEVEL_UNIMPLEMENTED);
		}
	} else if (major == GSS_S_CONTINUE_NEEDED) {
		rc = NDR_DRC_CONTINUE;
	}

	if (!smbd_spnego_token_to_ndr(mxa->heap, &gss_out_tok, out_tok,
	    out_size))
		rc = NDR_DRC_FAULT_SEC_SSP_FAILED;

	return (rc);
}

int
smbd_spnego_getmic(void *auth_ctx, ndr_xa_t *mxa)
{
	gss_buffer_desc gss_tok = {0}, gss_msg = {0};
	gss_ctx_id_t gss_ctx = (gss_ctx_id_t)auth_ctx;
	void **out_tok = (void **)&mxa->send_auth.auth_value;
	void *in_buf = mxa->send_nds.pdu_base_addr +
	    mxa->send_nds.pdu_body_offset;
	uint16_t *out_size = &mxa->send_hdr.common_hdr.auth_length;
	unsigned long in_size = mxa->send_nds.pdu_body_size;
	uint32_t major, minor;

	*out_size = 0;
	*out_tok = NULL;

	gss_msg.value = in_buf;
	gss_msg.length = (size_t)in_size;

	major = gss_get_mic(&minor, gss_ctx, GSS_C_QOP_DEFAULT, &gss_msg,
	    &gss_tok);

	if (GSS_ERROR(major)) {
		smbd_spnego_report_err(__func__, major, minor);
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);
	}

	if (!smbd_spnego_token_to_ndr(mxa->heap, &gss_tok, out_tok,
	    out_size))
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);

	return (NDR_DRC_OK);
}

int
smbd_spnego_verifymic(void *auth_ctx, ndr_xa_t *mxa, boolean_t verify_resp)
{
	gss_buffer_desc gss_tok = {0}, gss_msg = {0};
	gss_ctx_id_t gss_ctx = (gss_ctx_id_t)auth_ctx;
	void *in_tok = mxa->recv_auth.auth_value;
	void *in_buf = mxa->recv_nds.pdu_base_addr +
	    mxa->recv_nds.pdu_body_offset;
	unsigned long in_buf_size = mxa->recv_nds.pdu_body_size;
	uint32_t major, minor;
	uint16_t in_tok_size = mxa->recv_hdr.common_hdr.auth_length;

	gss_tok.value = in_tok;
	gss_tok.length = (size_t)in_tok_size;

	gss_msg.value = in_buf;
	gss_msg.length = (size_t)in_buf_size;

	major = gss_verify_mic(&minor, gss_ctx, &gss_msg, &gss_tok,
	    GSS_C_QOP_DEFAULT);

	if (GSS_ERROR(major)) {
		smbd_spnego_report_err(__func__, major, minor);

		if (verify_resp)
			return (NDR_DRC_FAULT_SEC_SIG_INVALID);
	}

	return (NDR_DRC_OK);
}

void
smbd_spnego_destroy_ctx(void **auth_ctx)
{
	gss_ctx_id_t *gss_ctx = (gss_ctx_id_t *)auth_ctx;
	uint32_t major, minor;

	if (*auth_ctx == NULL)
		return;

	major = gss_delete_sec_context(&minor, gss_ctx, GSS_C_NO_BUFFER);
	if (GSS_ERROR(major))
		smbd_spnego_report_err(__func__, major, minor);
}

const ndr_auth_ops_t smbd_spnego_ops = {
	.nao_accept = smbd_spnego_accept_sec_ctx,
	.nao_sign = smbd_spnego_getmic,
	.nao_verify = smbd_spnego_verifymic,
	.nao_destroy = smbd_spnego_destroy_ctx
};

void
smbd_pipesvc_register_ssp(void)
{
	if (!mlsvc_register_ssp(&smbd_spnego_ops, NDR_C_AUTHN_GSS_NEGOTIATE,
	    NDR_SSP_SUPPORTS_INTEGRITY))
		smbd_report("failed to register SSP; RPC auth won't work");
}
