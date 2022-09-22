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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Hacks to test NEGOEX bypass to NTLMSSP
 * Client sends { NEGOEX, NTLMSSP }
 * Server responds "New mech: NTLMSSP"
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <netdb.h>
#include <libintl.h>
#include <xti.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/byteorder.h>
#include <sys/socket.h>
#include <sys/fcntl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <netsmb/smb_lib.h>
#include <netsmb/mchain.h>

#include "private.h"
#include "charsets.h"
#include "spnego.h"
#include "derparse.h"
#include "ssp.h"

uchar_t negoex_req[] = {
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

static void
hexdump(const uchar_t *buf, int len)
{
	int idx;
	char ascii[24];
	char *pa = ascii;

	memset(ascii, '\0', sizeof (ascii));

	idx = 0;
	while (len--) {
		if ((idx & 15) == 0) {
			printf("%04X: ", idx);
			pa = ascii;
		}
		if (*buf > ' ' && *buf <= '~')
			*pa++ = *buf;
		else
			*pa++ = '.';
		printf("%02x ", *buf++);

		idx++;
		if ((idx & 3) == 0) {
			*pa++ = ' ';
			(void) putchar(' ');
		}
		if ((idx & 15) == 0) {
			*pa = '\0';
			printf("%s\n", ascii);
		}
	}

	if ((idx & 15) != 0) {
		*pa = '\0';
		/* column align the last ascii row */
		while ((idx & 15) != 0) {
			if ((idx & 3) == 0)
				(void) putchar(' ');
			printf("   ");
			idx++;
		}
		printf("%s\n", ascii);
	}
}

/*
 * Put canned negoex_req from above.
 */
int
ssp_hack_put_negoex(struct smb_ctx *ctx, struct mbdata *omb)
{
	struct mbuf *m;
	size_t toklen = sizeof(negoex_req);
	int err;

	err = mb_init_sz(omb, toklen);
	if (err)
		return (err);
	m = omb->mb_top;
	memcpy(m->m_data, negoex_req, toklen);
	omb->mb_count = m->m_len = toklen;

	return (0);
}

/*
 * Get negoex response (request-mic)
 */
int
ssp_hack_get_newmech(struct smb_ctx *ctx, struct mbdata *imb)
{
	struct mbuf *m;
	size_t toklen;
	int err;

	m = imb->mb_top;
	printf("NEGOEX response: (len=%d)\n", m->m_len);
	hexdump((uchar_t *)m->m_data, m->m_len);

	return (0);
}

/*
 * Put NTLMSSP negotiate, but as NegTokenTarg
 */
int
ssp_hack_put_newmech(struct smb_ctx *ctx, struct mbdata *caller_out)
{
	struct mbdata body_out;
	SPNEGO_TOKEN_HANDLE stok_out;
	SPNEGO_NEGRESULT result;
	ssp_ctx_t *sp;
	struct mbuf *m;
	ulong_t toklen;
	int err, rc;

	bzero(&body_out, sizeof (body_out));
	stok_out = NULL;
	sp = ctx->ct_ssp_ctx;

	err = sp->sp_nexttok(sp, NULL, &body_out);
	if (err)
		goto out;

	/*
	 * Wrap the outgoing body if requested,
	 * either negTokenInit on first call, or
	 * negTokenTarg on subsequent calls.
	 */
	if (caller_out != NULL) {
		m = body_out.mb_top;

		rc = spnegoCreateNegTokenTarg(
		    sp->sp_mech,
		    spnego_negresult_NotUsed,
		    (uchar_t *)m->m_data, m->m_len,
		    NULL, 0, &stok_out);
		/* Note: allocated stok_out */

		if (rc) {
			DPRINT("CreateNegTokenX, rc 0x%x", rc);
			err = EBADRPC;
			goto out;
		}

		/*
		 * Copy binary from stok_out to caller_out
		 * Two calls: get the size, get the data.
		 */
		rc = spnegoTokenGetBinary(stok_out, NULL, &toklen);
		if (rc != SPNEGO_E_BUFFER_TOO_SMALL) {
			DPRINT("GetBinary1, rc 0x%x", rc);
			err = EBADRPC;
			goto out;
		}
		err = mb_init_sz(caller_out, (size_t)toklen);
		if (err)
			goto out;
		m = caller_out->mb_top;
		rc = spnegoTokenGetBinary(stok_out,
		    (uchar_t *)m->m_data, &toklen);
		if (rc) {
			DPRINT("GetBinary2, rc 0x%x", rc);
			err = EBADRPC;
			goto out;
		}
		caller_out->mb_count = m->m_len = (size_t)toklen;
	}

	err = 0;

out:
	mb_done(&body_out);
	spnegoFreeData(stok_out);

	return (err);
}
