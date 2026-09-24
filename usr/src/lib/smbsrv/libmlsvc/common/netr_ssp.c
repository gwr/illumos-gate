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
 * Copyright 2020 Tintri by DDN, Inc. All Rights Reserved.
 * Copyright 2023-2026 RackTop Systems Inc.
 */

#include <sys/md5.h>
#include <sys/sha2.h>
#include <sys/debug.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <smbsrv/netrauth.h>
#include <smbsrv/string.h>
#include <smbsrv/libsmb.h>
#include <libmlsvc.h>
#include <resolv.h>

/*
 * NETLOGON SSP for "Secure RPC" works as follows:
 * 1. The client binds to the DC without RPC-level authentication.
 * 2. The client and server negotiate a Session Key using a client
 * and server challenge, plus a shared secret (the machine password).
 * This happens via NetrServerReqChallenge and NetrServerAuthenticate.
 * The key is bound to a particular Computer/Server Name pair.
 * 3. The client then establishes a new bind (or alters its existing one),
 * this time requesting the NETLOGON provider for RPC-level authentication.
 * The server uses the Computer and Domain names provided in the
 * authentication token in the bind request in order to find
 * the previously-negotiated Session Key (and rejects the bind if none
 * exists).
 * 4. The client and server then use this Session Key to provide
 * integrity and/or confidentiality to future NETLOGON RPC messages.
 *
 * The functions in this file implement the NETLOGON SSP, as defined in
 * [MS-NRPC] 3.3 "Netlogon as a Security Support Provider".
 *
 * Session Key negotiation is implemented in netr_auth.c.
 * It is the same key that is used for generating NETLOGON credentials.
 */

enum nl_token_type {
	NL_AUTH_REQUEST = 0x00000000,
	NL_AUTH_RESPONSE = 0x00000001
};

/*
 * DOMAIN = domain name
 * COMPUTER = client computer name
 * HOST = client host name
 *
 * NB = NetBios format
 * DNS = FQDN
 *
 * OEM = OEM_STRING
 * COMPRESSED = Compressed UTF-8 string
 *
 * Each of these is NULL-terminated, and delinated by such.
 * They are always found in this order, when specified.
 *
 * We currently use everything but NL_HOST_DNS_COMPRESSED_FLAG.
 */
#define	NL_DOMAIN_NB_OEM_FLAG		0x00000001
#define	NL_COMPUTER_NB_OEM_FLAG		0x00000002
#define	NL_DOMAIN_DNS_COMPRESSED_FLAG	0x00000004
#define	NL_HOST_DNS_COMPRESSED_FLAG	0x00000008
#define	NL_COMPUTER_NB_COMPRESSED_FLAG	0x00000010

#define	NL_DOMAIN_FLAGS			\
	(NL_DOMAIN_NB_OEM_FLAG|NL_DOMAIN_DNS_COMPRESSED_FLAG)
#define	NL_COMPUTER_FLAGS		\
	(NL_COMPUTER_NB_OEM_FLAG|		\
	NL_HOST_DNS_COMPRESSED_FLAG|		\
	NL_COMPUTER_NB_COMPRESSED_FLAG)

/*
 * These structures are OPAQUE at the RPC level - not marshalled.
 * Multi-byte integer members are little-endian on the wire, so access
 * them using the LE_IN* and LE_OUT* macros.
 */
typedef struct nl_auth_message {
	uint32_t nam_type;
	uint32_t nam_flags;
	uchar_t nam_str[1];
} nl_auth_message_t;

/*
 * The size of this structure is used for space accounting.
 * The confounder is not present on the wire unless confidentiality
 * has been negotiated. When generating a token, we'll adjust space accounting
 * based on whether the confounder is present.
 */
#define	NL_AUTH_CONFOUNDER_SIZE	8
#define	NL_AUTH_SHA2_RESERVED_SIZE	24
typedef struct nl_auth_sig {
	uint16_t nas_sig_alg;
	uint16_t nas_seal_alg;
	uint16_t nas_pad;
	uint16_t nas_flags;
	uchar_t nas_seqnum[8];
	uchar_t nas_sig[8];
	uchar_t nas_tail[NL_AUTH_CONFOUNDER_SIZE + NL_AUTH_SHA2_RESERVED_SIZE];
} nl_auth_sig_t;

#define	NL_AUTH_SIG_SIGN_SIZE	24
#define	NL_AUTH_SIG_SEAL_SIZE	32
#define	NL_AUTH_SHA2_SIG_SIGN_SIZE	48
#define	NL_AUTH_SHA2_SIG_SEAL_SIZE	56

static uchar_t *
netr_ssp_confounder(nl_auth_sig_t *nas)
{
	return (nas->nas_tail);
}

static uchar_t *
netr_ssp_reserved(nl_auth_sig_t *nas, boolean_t has_confounder)
{
	return (nas->nas_tail + (has_confounder ? NL_AUTH_CONFOUNDER_SIZE : 0));
}

void
netr_show_msg(nl_auth_message_t *nam, ndr_stream_t *nds)
{
	ndo_printf(nds, NULL, "nl_auth_message: type=0x%x flags=0x%x",
	    LE_IN32(&nam->nam_type), LE_IN32(&nam->nam_flags));
}

void
netr_show_sig(nl_auth_sig_t *nas, ndr_stream_t *nds)
{
	ndo_printf(nds, NULL, "nl_auth_sig: SignatureAlg=0x%x SealAlg=0x%x "
	    "pad=0x%x flags=0x%x SequenceNumber=%llu Signature=0x%x "
	    "Confounder=0x%x",
	    LE_IN16(&nas->nas_sig_alg), LE_IN16(&nas->nas_seal_alg),
	    LE_IN16(&nas->nas_pad), LE_IN16(&nas->nas_flags),
	    *(uint64_t *)nas->nas_seqnum,
	    *(uint64_t *)nas->nas_sig,
	    *(uint64_t *)netr_ssp_confounder(nas));
}

/*
 * NETLOGON SSP gss_init_sec_context equivalent
 * [MS-NRPC] 3.3.4.1.1 "Generating an Initial NL_AUTH_MESSAGE"
 *
 * We need to encode at least one Computer name and at least one
 * Domain name. The server uses this to find the Session Key
 * negotiated earlier between this client and server.
 *
 * We attempt to provide NL_DOMAIN_NB_OEM_FLAG, NL_COMPUTER_NB_OEM_FLAG,
 * NL_DOMAIN_DNS_COMPRESSED_FLAG, and NL_COMPUTER_NB_COMPRESSED_FLAG.
 *
 * See the above comments for how these are encoded.
 */
int
netr_ssp_init(void *arg, ndr_xa_t *mxa)
{
	netr_info_t *auth = arg;
	ndr_common_header_t *hdr = &mxa->send_hdr.common_hdr;
	nl_auth_message_t *nam;
	size_t domain_len, comp_len, len;
	uint32_t flags = 0;
	int slen;
	uchar_t *dnptrs[3], **dnlastptr;

	domain_len = smb_sbequiv_strlen(auth->nb_domain);
	comp_len = smb_sbequiv_strlen(auth->hostname);

	/*
	 * Need to allocate length for two OEM_STRINGs + NULL bytes, plus space
	 * sufficient for two NULL-terminated compressed UTF-8 strings.
	 * For the UTF-8 strings, use 2*len as a heuristic.
	 */
	len = domain_len + 1 + comp_len + 1 +
	    strlen(auth->hostname) * 2 + strlen(auth->fqdn_domain) * 2;

	hdr->auth_length = 0;

	nam = NDR_MALLOC(mxa, len);
	if (nam == NULL)
		return (NDR_DRC_FAULT_SEC_OUT_OF_MEMORY);

	if (domain_len != -1) {
		slen = smb_mbstooem(nam->nam_str, auth->nb_domain, domain_len);
		if (slen >= 0) {
			hdr->auth_length += slen + 1;
			nam->nam_str[hdr->auth_length - 1] = '\0';
			flags |= NL_DOMAIN_NB_OEM_FLAG;
		}
	}

	if (comp_len != -1) {
		slen = smb_mbstooem(nam->nam_str + hdr->auth_length,
		    auth->hostname, comp_len);
		if (slen >= 0) {
			hdr->auth_length += slen + 1;
			nam->nam_str[hdr->auth_length - 1] = '\0';
			flags |= NL_COMPUTER_NB_OEM_FLAG;
		}
	}

	dnptrs[0] = NULL;
	dnlastptr = &dnptrs[sizeof (dnptrs) / sizeof (dnptrs[0])];

	slen = dn_comp(auth->fqdn_domain, nam->nam_str + hdr->auth_length,
	    len - hdr->auth_length, dnptrs, dnlastptr);

	if (slen >= 0) {
		hdr->auth_length += slen;
		nam->nam_str[hdr->auth_length] = '\0';
		flags |= NL_DOMAIN_DNS_COMPRESSED_FLAG;
	}

	slen = dn_comp(auth->hostname, nam->nam_str + hdr->auth_length,
	    len - hdr->auth_length, dnptrs, dnlastptr);
	if (slen >= 0) {
		hdr->auth_length += slen;
		nam->nam_str[hdr->auth_length] = '\0';
		flags |= NL_COMPUTER_NB_COMPRESSED_FLAG;
	}

	/* We must provide at least one Domain Name and Computer Name */
	if ((flags & NL_DOMAIN_FLAGS) == 0 ||
	    (flags & NL_COMPUTER_FLAGS) == 0)
		return (NDR_DRC_FAULT_SEC_ENCODE_FAILED);

	LE_OUT32(&nam->nam_type, NL_AUTH_REQUEST);
	LE_OUT32(&nam->nam_flags, flags);

	mxa->send_auth.auth_value = (void *)nam;
	hdr->auth_length += sizeof (nam->nam_flags) + sizeof (nam->nam_type);

	return (0);
}

/*
 * NETLOGON SSP response-side gss_init_sec_context equivalent
 * [MS-NRPC] 3.3.4.1.4 "Receiving a Return NL_AUTH_MESSAGE"
 */
int
netr_ssp_recv(void *arg, ndr_xa_t *mxa)
{
	netr_info_t *auth = arg;
	ndr_common_header_t *ahdr = &mxa->recv_hdr.common_hdr;
	ndr_sec_t *ack_secp = &mxa->recv_auth;
	nl_auth_message_t *nam;

	nam = (nl_auth_message_t *)ack_secp->auth_value;

	/*
	 * We only need to verify the length ("at least 12") and the type.
	 * Don't show a message that is too short to hold its header.
	 */
	if (ahdr->auth_length < 12) {
		ndo_printf(&mxa->recv_nds, NULL,
		    "nl_auth_message: short length: %u", ahdr->auth_length);
		return (NDR_DRC_FAULT_SEC_AUTH_LENGTH_INVALID);
	}
	if (LE_IN32(&nam->nam_type) != NL_AUTH_RESPONSE) {
		netr_show_msg(nam, &mxa->recv_nds);
		return (NDR_DRC_FAULT_SEC_META_INVALID);
	}
	auth->clh_seqnum = 0;

	return (NDR_DRC_OK);
}

/* returns byte N of seqnum */
#define	CLS_BYTE(n, seqnum) ((seqnum >> (8 * (n))) & 0xff)

/*
 * Perform key derivation using HMAC-MD5, as specified in MS-NRPC.
 * This generates an intermediate key using HMAC-MD5 over zeroes using
 * the input key, then generates the final key using HMAC-MD5 over some
 * caller-specified data using the intermediate key.
 * See callers for spec references (there are multiple!).
 */
int
netr_ssp_derive_key(uchar_t *input_key, uint_t key_len,
    uchar_t *data, uint_t data_len, uchar_t *out_key)
{
	int rc;
	uint32_t zeroes = 0;

	rc = smb_auth_hmac_md5((uchar_t *)&zeroes, 4,
	    input_key, key_len,
	    out_key);

	if (rc == 0)
		rc = smb_auth_hmac_md5(data, data_len,
		    out_key, MD_DIGEST_LEN,
		    out_key);

	return (rc);
}

/*
 * The HMAC-MD5 signature is computed as follows:
 * First 8 bytes of
 * HMAC_MD5(
 *	MD5(0x00000000 | sig_alg | seal_alg | pad | flags |
 *		<confounder if present> | PDU body),
 *	session_key)
 * (caller handles truncation)
 */
static int
netr_ssp_md5_sig(netr_info_t *auth, nl_auth_sig_t *nas,
    uchar_t *confounder, uchar_t *pdu_body, size_t pdu_body_size,
    uchar_t *sig)
{
	uint32_t zeroes = 0;
	MD5_CTX md5h;

	MD5Init(&md5h);
	MD5Update(&md5h, (uchar_t *)&zeroes, 4);
	MD5Update(&md5h, (uchar_t *)nas, 8);
	if (confounder != NULL)
		MD5Update(&md5h, confounder, NL_AUTH_CONFOUNDER_SIZE);
	MD5Update(&md5h, pdu_body, pdu_body_size);
	MD5Final(sig, &md5h);

	return (smb_auth_hmac_md5(sig, MD_DIGEST_LEN,
	    auth->session_key.key, auth->session_key.len, sig));
}

/*
 * The SHA256 signature is computed as follows:
 * First 8 bytes of
 * HMAC_SHA256(sig_alg | seal_alg | pad | flags |
 *	<confounder if present> | PDU body, session_key)
 * (caller handles truncation)
 */
static int
netr_ssp_sha256_sig(netr_info_t *auth, nl_auth_sig_t *nas,
    uchar_t *confounder, uchar_t *pdu_body, size_t pdu_body_size,
    uchar_t *sig)
{
	iovec_t data[3];
	size_t count = 0;

	data[count].iov_base = (caddr_t)nas;
	data[count++].iov_len = 8;
	if (confounder != NULL) {
		data[count].iov_base = (caddr_t)confounder;
		data[count++].iov_len = NL_AUTH_CONFOUNDER_SIZE;
	}
	data[count].iov_base = (caddr_t)pdu_body;
	data[count++].iov_len = pdu_body_size;

	return (smb_auth_hmac_sha256(data, count, auth->session_key.key,
	    auth->session_key.len, sig));
}

/*
 * The confounder and body use independent RC4 streams with the same key.
 * The EncryptionKey is computed as follows:
 * XorKey = (XOR each byte of session_key with 0xf0)
 * HMAC_MD5(local_seqnum, HMAC_MD5(0x00000000, xor_key))
 */
static int
netr_ssp_rc4_seal(netr_info_t *auth, uchar_t *seqnum,
    uchar_t *confounder_in, uchar_t *confounder_out,
    uchar_t *pdu_body, size_t pdu_body_size)
{
	BYTE key[MD_DIGEST_LEN];
	int rc;

	rc = netr_ssp_derive_key(auth->rpc_seal_key.key,
	    auth->rpc_seal_key.len, seqnum, 8, key);
	if (rc == 0)
		rc = smb_auth_RC4(confounder_out, NL_AUTH_CONFOUNDER_SIZE,
		    key, sizeof (key), confounder_in, NL_AUTH_CONFOUNDER_SIZE);
	if (rc == 0)
		rc = smb_auth_RC4(pdu_body, pdu_body_size,
		    key, sizeof (key), pdu_body, pdu_body_size);

	explicit_bzero(key, sizeof (key));
	return (rc);
}

/*
 * The confounder and body share one AES-CFB8 operation.
 * The EncryptionKey is computed as: XOR each byte of session_key with 0xf0
 * (That's exactly auth->rpc_seal_key).
 */
static int
netr_ssp_aes_seal(netr_info_t *auth, uchar_t *seqnum,
    uchar_t *confounder_in, uchar_t *confounder_out,
    uchar_t *pdu_body, size_t pdu_body_size, boolean_t decrypt)
{
	uchar_t iv[16];
	iovec_t input[2];
	iovec_t output[2];
	int rc;

	bcopy(seqnum, iv, 8);
	bcopy(seqnum, iv + 8, 8);
	input[0].iov_base = (caddr_t)confounder_in;
	input[0].iov_len = NL_AUTH_CONFOUNDER_SIZE;
	output[0].iov_base = (caddr_t)confounder_out;
	output[0].iov_len = NL_AUTH_CONFOUNDER_SIZE;
	input[1].iov_base = (caddr_t)pdu_body;
	input[1].iov_len = pdu_body_size;
	output[1].iov_base = (caddr_t)pdu_body;
	output[1].iov_len = pdu_body_size;
	rc = smb_auth_AES_CFB8(input, output, 2, auth->rpc_seal_key.key,
	    auth->rpc_seal_key.len, iv, decrypt);

	explicit_bzero(iv, sizeof (iv));
	return (rc);
}

/*
 * For RC4 Encryption, the EncryptionKey is computed as follows:
 * HMAC_MD5(signature, HMAC_MD5(0x00000000, session_key))
 */
static int
netr_ssp_rc4_seqnum(netr_info_t *auth, nl_auth_sig_t *nas)
{
	BYTE key[MD_DIGEST_LEN];
	int rc;

	rc = netr_ssp_derive_key(auth->session_key.key,
	    auth->session_key.len, nas->nas_sig, sizeof (nas->nas_sig), key);
	if (rc == 0)
		rc = smb_auth_RC4(nas->nas_seqnum, sizeof (nas->nas_seqnum),
		    key, sizeof (key), nas->nas_seqnum,
		    sizeof (nas->nas_seqnum));

	explicit_bzero(key, sizeof (key));
	return (rc);
}

/*
 * For AES Encryption, the EncryptionKey is exactly the SessionKey.
 */
static int
netr_ssp_aes_seqnum(netr_info_t *auth, nl_auth_sig_t *nas, boolean_t decrypt)
{
	int rc;
	uint8_t iv[16];
	iovec_t data;

	bcopy(nas->nas_sig, iv, sizeof (nas->nas_sig));
	bcopy(nas->nas_sig, iv + sizeof (nas->nas_sig),
	    sizeof (nas->nas_sig));
	data.iov_base = (caddr_t)nas->nas_seqnum;
	data.iov_len = sizeof (nas->nas_seqnum);
	rc = smb_auth_AES_CFB8(&data, &data, 1, auth->session_key.key,
	    auth->session_key.len, iv, decrypt);

	explicit_bzero(&iv, sizeof (iv));
	return (rc);
}

/*
 * [MS-NRPC] 3.3.4.2.1 "Generating a Client Netlogon Signature Token"
 *
 * Set up the metadata, encrypt and increment the SequenceNumber,
 * and sign the PDU body. If a confounder is provided, encrypt it and
 * the PDU body.
 * Encryption happens here because the RC4 key is derived from data
 * generated locally for signing/verification.
 */
int
netr_ssp_make_token(netr_info_t *auth, ndr_stream_t *nds, nl_auth_sig_t *nas,
    uchar_t *confounder)
{
	BYTE local_sig[SHA256_DIGEST_LENGTH];
	uchar_t *pdu_body = nds->pdu_base_addr + nds->pdu_body_offset;
	uint16_t sig_alg, seal_alg;

	CTASSERT(SHA256_DIGEST_LENGTH >= MD_DIGEST_LEN);

	/*
	 * SignatureAlgorithm is first byte 0x77, second byte 00 for HMAC-MD5
	 * or 0x13, 0x00 for AES-HMAC-SHA256.
	 *
	 * SealAlgorithm is first byte 0x7A, second byte 00 for RC4
	 * or 0x1A, 0x00 for AES-CFB8, or 0xffff for No Sealing.
	 *
	 * Pad is always 0xffff, and flags is always 0x0000.
	 *
	 * SequenceNumber is a computed, encrypted, 64-bit number.
	 *
	 * Each of these is always encoded in little-endian order.
	 */
	sig_alg = auth->use_aes ? 0x0013 : 0x0077;
	if (confounder != NULL)
		seal_alg = auth->use_aes ? 0x001A : 0x007A;
	else
		seal_alg = 0xffff;
	LE_OUT16(&nas->nas_sig_alg, sig_alg);
	LE_OUT16(&nas->nas_seal_alg, seal_alg);
	LE_OUT16(&nas->nas_pad, 0xffff);
	LE_OUT16(&nas->nas_flags, 0);
	bzero(netr_ssp_reserved(nas, confounder != NULL),
	    NL_AUTH_SHA2_RESERVED_SIZE);

	/*
	 * Calculate the SequenceNumber.
	 * Note that byte 4 gets modified, as per the spec -
	 * It's the only byte that is not just set to some other byte.
	 */
	nas->nas_seqnum[0] = CLS_BYTE(3, auth->clh_seqnum);
	nas->nas_seqnum[1] = CLS_BYTE(2, auth->clh_seqnum);
	nas->nas_seqnum[2] = CLS_BYTE(1, auth->clh_seqnum);
	nas->nas_seqnum[3] = CLS_BYTE(0, auth->clh_seqnum);
	nas->nas_seqnum[4] = CLS_BYTE(7, auth->clh_seqnum) | 0x80;
	nas->nas_seqnum[5] = CLS_BYTE(6, auth->clh_seqnum);
	nas->nas_seqnum[6] = CLS_BYTE(5, auth->clh_seqnum);
	nas->nas_seqnum[7] = CLS_BYTE(4, auth->clh_seqnum);

	auth->clh_seqnum++;

	/*
	 * Compute the Signature.
	 * Note that this is based on the unencrypted data.
	 */
	if (auth->use_aes) {
		if (netr_ssp_sha256_sig(auth, nas, confounder, pdu_body,
		    nds->pdu_body_size, local_sig) != SMBAUTH_SUCCESS)
			return (NDR_DRC_FAULT_SEC_SSP_FAILED);
	} else if (netr_ssp_md5_sig(auth, nas, confounder, pdu_body,
	    nds->pdu_body_size, local_sig) != 0)
		return (NDR_DRC_FAULT_SEC_SSP_FAILED);

	bcopy(local_sig, nas->nas_sig, 8);

	/*
	 * [MS-NRPC] 3.3.4.2.1 "Generating a Client Netlogon Signature Token"
	 * Encrypt the Confounder and PDU Body.
	 */
	if (confounder != NULL) {
		if (auth->use_aes) {
			if (netr_ssp_aes_seal(auth, nas->nas_seqnum,
			    confounder, netr_ssp_confounder(nas), pdu_body,
			    nds->pdu_body_size, B_FALSE) != SMBAUTH_SUCCESS)
				goto errout;
		} else if (netr_ssp_rc4_seal(auth, nas->nas_seqnum,
		    confounder, netr_ssp_confounder(nas), pdu_body,
		    nds->pdu_body_size) != 0)
			goto errout;
	}

	/*
	 * Encrypt the SequenceNumber.
	 */
	if (auth->use_aes) {
		if (netr_ssp_aes_seqnum(auth, nas, B_FALSE) != 0)
			goto errout;
	} else if (netr_ssp_rc4_seqnum(auth, nas) != 0)
		goto errout;

	return (NDR_DRC_OK);

errout:
	return (NDR_DRC_FAULT_SEC_SSP_FAILED);
}

/*
 * [MS-NRPC] 3.3.4.2.4 "Receiving a Server Netlogon Signature Token"
 *
 * Verify the metadata; decrypt, verify, and increment the SequenceNumber;
 * and validate the PDU body against the provided signature.
 * If the confounder is present, decrypt it and the PDU body. This is done here
 * because the key relies on local_seqnum ("CopySeqNumber"), which can only be
 * retrieved here.
 */
int
netr_ssp_check_token(netr_info_t *auth, ndr_stream_t *nds, nl_auth_sig_t *nas,
    boolean_t verify_resp, boolean_t has_confounder)
{
	BYTE local_sig[SHA256_DIGEST_LENGTH];
	BYTE local_seqnum[8];
	uchar_t confounder[NL_AUTH_CONFOUNDER_SIZE];
	uchar_t *pdu_body = nds->pdu_base_addr + nds->pdu_body_offset;
	int rc;
	boolean_t seqnum_bumped = B_FALSE;

	/*
	 * Verify SignatureAlgorithm, SealAlgorithm, and Pad are as expected.
	 * These follow the same values as in the Client Signature.
	 */
	if (LE_IN16(&nas->nas_sig_alg) != (auth->use_aes ? 0x0013 : 0x0077) ||
	    LE_IN16(&nas->nas_seal_alg) != (has_confounder ?
	    (auth->use_aes ? 0x001A : 0x007A) : 0xffff) ||
	    LE_IN16(&nas->nas_pad) != 0xffff) {
		rc = NDR_DRC_FAULT_SEC_META_INVALID;
		goto errout;
	}

	/* Decrypt the SequenceNumber. This is done the same as the Client. */
	if (auth->use_aes) {
		if (netr_ssp_aes_seqnum(auth, nas, B_TRUE) != 0) {
			rc = NDR_DRC_FAULT_SEC_SSP_FAILED;
			goto errout;
		}
	} else if (netr_ssp_rc4_seqnum(auth, nas) != 0) {
		rc = NDR_DRC_FAULT_SEC_SSP_FAILED;
		goto errout;
	}

	/*
	 * Calculate a local version of the SequenceNumber.
	 * Note that byte 4 does NOT get modified, unlike the client.
	 */
	local_seqnum[0] = CLS_BYTE(3, auth->clh_seqnum);
	local_seqnum[1] = CLS_BYTE(2, auth->clh_seqnum);
	local_seqnum[2] = CLS_BYTE(1, auth->clh_seqnum);
	local_seqnum[3] = CLS_BYTE(0, auth->clh_seqnum);
	local_seqnum[4] = CLS_BYTE(7, auth->clh_seqnum);
	local_seqnum[5] = CLS_BYTE(6, auth->clh_seqnum);
	local_seqnum[6] = CLS_BYTE(5, auth->clh_seqnum);
	local_seqnum[7] = CLS_BYTE(4, auth->clh_seqnum);

	/* If the SequenceNumbers don't match, this is out of order - drop it */
	if (bcmp(local_seqnum, nas->nas_seqnum, sizeof (local_seqnum)) != 0) {
		ndo_printf(nds, NULL, "CalculatedSeqnum: %llu "
		    "DecryptedSeqnum: %llu",
		    *(uint64_t *)local_seqnum, *(uint64_t *)nas->nas_seqnum);
		rc = NDR_DRC_FAULT_SEC_SEQNUM_INVALID;
		goto errout;
	}

	auth->clh_seqnum++;
	seqnum_bumped = B_TRUE;

	/*
	 * Decrypt the Confounder and PDU Body.
	 */
	if (has_confounder) {
		if (auth->use_aes) {
			if (netr_ssp_aes_seal(auth, local_seqnum,
			    netr_ssp_confounder(nas), confounder, pdu_body,
			    nds->pdu_body_size, B_TRUE) != SMBAUTH_SUCCESS) {
				rc = NDR_DRC_FAULT_SEC_SSP_FAILED;
				goto errout;
			}
		} else if (netr_ssp_rc4_seal(auth, local_seqnum,
		    netr_ssp_confounder(nas), confounder, pdu_body,
		    nds->pdu_body_size) != 0) {
			rc = NDR_DRC_FAULT_SEC_SSP_FAILED;
			goto errout;
		}
	}

	/*
	 * Calculate the signature.
	 * This is done the same as the Client.
	 * The signature is based on the unencrypted data.
	 */
	if (auth->use_aes) {
		if (netr_ssp_sha256_sig(auth, nas,
		    has_confounder ? confounder : NULL, pdu_body,
		    nds->pdu_body_size, local_sig) != SMBAUTH_SUCCESS) {
			rc = NDR_DRC_FAULT_SEC_SSP_FAILED;
			goto errout;
		}
	} else if (netr_ssp_md5_sig(auth, nas,
	    has_confounder ? confounder : NULL, pdu_body,
	    nds->pdu_body_size, local_sig) != 0) {
		rc = NDR_DRC_FAULT_SEC_SSP_FAILED;
		goto errout;
	}

	/* If the first 8 bytes don't match, drop it */
	if (bcmp(local_sig, nas->nas_sig, 8) != 0) {
		ndo_printf(nds, NULL, "CalculatedSig: %llu "
		    "PacketSig: %llu",
		    *(uint64_t *)local_sig, *(uint64_t *)nas->nas_sig);
		rc = NDR_DRC_FAULT_SEC_SIG_INVALID;
		goto errout;
	}

	return (NDR_DRC_OK);

errout:
	netr_show_sig(nas, nds);

	if (!verify_resp) {
		if (!seqnum_bumped)
			auth->clh_seqnum++;
		return (NDR_DRC_OK);
	}

	return (rc);
}

/*
 * NETLOGON SSP gss_MICEx equivalent.
 * Note that auth_length does NOT include the length of the confounder.
 */
int
netr_ssp_sign(void *arg, ndr_xa_t *mxa)
{
	netr_info_t *auth = arg;
	ndr_common_header_t *hdr = &mxa->send_hdr.common_hdr;
	nl_auth_sig_t *nas;

	hdr->auth_length = auth->use_aes ? NL_AUTH_SHA2_SIG_SIGN_SIZE :
	    NL_AUTH_SIG_SIGN_SIZE;
	CTASSERT(NL_AUTH_SIG_SIGN_SIZE ==
	    (offsetof(nl_auth_sig_t, nas_sig) + sizeof (nas->nas_sig)));
	CTASSERT(NL_AUTH_SHA2_SIG_SIGN_SIZE ==
	    offsetof(nl_auth_sig_t, nas_tail) + NL_AUTH_SHA2_RESERVED_SIZE);

	nas = NDR_MALLOC(mxa, sizeof (*nas));
	if (nas == NULL)
		return (NDR_DRC_FAULT_SEC_OUT_OF_MEMORY);

	mxa->send_auth.auth_value = (void *)nas;

	return (netr_ssp_make_token(arg, &mxa->send_nds, nas, NULL));
}

/*
 * NETLOGON SSP gss_VerifyMICEx equivalent.
 */
int
netr_ssp_verify(void *arg, ndr_xa_t *mxa, boolean_t verify_resp)
{
	netr_info_t *auth = arg;
	ndr_common_header_t *hdr = &mxa->recv_hdr.common_hdr;
	ndr_sec_t *secp = &mxa->recv_auth;
	nl_auth_sig_t *nas = (nl_auth_sig_t *)secp->auth_value;

	if (hdr->auth_length < (auth->use_aes ? NL_AUTH_SHA2_SIG_SIGN_SIZE :
	    NL_AUTH_SIG_SIGN_SIZE))
		return (NDR_DRC_FAULT_SEC_AUTH_LENGTH_INVALID);

	return (netr_ssp_check_token(arg, &mxa->recv_nds, nas, verify_resp,
	    B_FALSE));
}

/*
 * NETLOGON SSP gss_WrapEx equivalent.
 * Generate a cryptographically random confounder before signing/sealing.
 * It is included in auth_length here.
 */
int
netr_ssp_encrypt(void *arg, ndr_xa_t *mxa)
{
	netr_info_t *auth = arg;
	uchar_t confounder[NL_AUTH_CONFOUNDER_SIZE];
	ndr_common_header_t *hdr = &mxa->send_hdr.common_hdr;
	nl_auth_sig_t *nas;

	hdr->auth_length = auth->use_aes ? NL_AUTH_SHA2_SIG_SEAL_SIZE :
	    NL_AUTH_SIG_SEAL_SIZE;
	CTASSERT(NL_AUTH_SIG_SEAL_SIZE ==
	    offsetof(nl_auth_sig_t, nas_tail) + NL_AUTH_CONFOUNDER_SIZE);
	CTASSERT(NL_AUTH_SHA2_SIG_SEAL_SIZE == sizeof (nl_auth_sig_t));

	nas = NDR_MALLOC(mxa, sizeof (*nas));
	if (nas == NULL)
		return (NDR_DRC_FAULT_SEC_OUT_OF_MEMORY);

	mxa->send_auth.auth_value = (void *)nas;

	arc4random_buf(confounder, NL_AUTH_CONFOUNDER_SIZE);
	return (netr_ssp_make_token(arg, &mxa->send_nds, nas, confounder));
}

/*
 * NETLOGON SSP gss_UnwrapEx equivalent.
 * Note that the Confounder is only used for calculating the signature.
 */
int
netr_ssp_decrypt(void *arg, ndr_xa_t *mxa, boolean_t verify_resp)
{
	netr_info_t *auth = arg;
	ndr_common_header_t *hdr = &mxa->recv_hdr.common_hdr;
	ndr_sec_t *secp = &mxa->recv_auth;
	nl_auth_sig_t *nas = (nl_auth_sig_t *)secp->auth_value;

	if (hdr->auth_length < (auth->use_aes ? NL_AUTH_SHA2_SIG_SEAL_SIZE :
	    NL_AUTH_SIG_SEAL_SIZE))
		return (NDR_DRC_FAULT_SEC_AUTH_LENGTH_INVALID);

	return (netr_ssp_check_token(arg, &mxa->recv_nds, nas, verify_resp,
	    B_TRUE));
}

extern struct netr_info netr_global_info;

ndr_auth_ctx_t netr_ssp_ctx = {
	.auth_ops = {
		.nao_init = netr_ssp_init,
		.nao_recv = netr_ssp_recv,
		.nao_sign = netr_ssp_sign,
		.nao_verify = netr_ssp_verify,
		.nao_encrypt = netr_ssp_encrypt,
		.nao_decrypt = netr_ssp_decrypt
	},
	.auth_ctx = &netr_global_info,
	.auth_context_id = 0,
	.auth_type = NDR_C_AUTHN_GSS_NETLOGON,
	.auth_level = NDR_C_AUTHN_LEVEL_PKT_PRIVACY,
	.auth_verify_resp = B_TRUE
};
