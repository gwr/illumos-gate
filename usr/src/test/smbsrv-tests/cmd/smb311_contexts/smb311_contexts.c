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
 * Copyright 2022 Tintri by DDN, Inc. All rights reserved.
 */

/*
 * Test SMB 3.1.1 Negotiation Contexts.
 * Currently just Preauthentication Integrity and Encryption.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <ctype.h>

#include <sys/byteorder.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct msg_buf {
	char *buf;
	size_t offset;
	size_t len;
} msg_buf_t;

#define	SMB2_NEGO_REQ_STRUCT_SIZE 36
#define	SMB2_GLOBAL_CAP_ENCRYPTION 0x40
#define	SMB2_HDR_LEN 64
#define	NEG_CTX_OFFSET (SMB2_HDR_LEN + 40)
#define	SMB3_PROTO_311	0x0311
#define	SMB2_PREAUTH_CAPS 0x0001
#define	SMB2_ENCRYPTION_CAPS 0x0002
#define	NEG_SALT_LEN 32
#define	PREAUTH_HDR (2 + 2 + 32) /* algcnt + saltlen + salt */
#define	ENCRYPT_HDR (2) /* algcnt */
#define	RESP_HDR_STATUS_OFFSET 8
#define	NEG_RESP_COUNT_OFFSET (SMB2_HDR_LEN + 6)
#define	NEG_RESP_OFFSET_OFFSET (SMB2_HDR_LEN + 60)
#define	SMB2_PROTOID 0x424D53FE
#define	SMB2_HDR_STRUCTSZ 64
#define	SMB2_NEGO_CMD 0x0000

void
msg_put_short(msg_buf_t *msg, uint16_t val)
{
	if ((msg->len - msg->offset) < sizeof (val)) {
		fprintf(stderr, "no room: size %d remaining %d\n",
		    sizeof (val), msg->len - msg->offset);
		exit(10);
	}

	msg->buf[msg->offset++] = (char)val;
	msg->buf[msg->offset++] = (char)(val >> 8);
}

void
msg_put_int(msg_buf_t *msg, uint32_t val)
{
	if ((msg->len - msg->offset) < sizeof (val)) {
		fprintf(stderr, "no room: size %d remaining %d\n",
		    sizeof (val), msg->len - msg->offset);
		exit(10);
	}

	msg->buf[msg->offset++] = (char)val;
	msg->buf[msg->offset++] = (char)(val >> 8);
	msg->buf[msg->offset++] = (char)(val >> 16);
	msg->buf[msg->offset++] = (char)(val >> 24);
}

void
msg_align(msg_buf_t *msg, uint8_t align)
{
	uint8_t padsz = P2NPHASE(msg->offset, align);
	int i;

	if ((msg->len - msg->offset) < padsz) {
		fprintf(stderr, "no room: size %d remaining %d\n",
		    padsz, msg->len - msg->offset);
		exit(10);
	}
	for (i = 0; i < padsz; i++)
		msg->buf[msg->offset++] = 0;
}

void
msg_put_hdr(msg_buf_t *msg)
{
	/* make room for NBHDR */
	msg->buf += 4;
	msg->len -= 4;

	msg_put_int(msg, SMB2_PROTOID); /* ProtocolID */
	msg_put_short(msg, SMB2_HDR_STRUCTSZ); /* StructureSize */
	msg_put_short(msg, 0); /* CreditCharge */
	msg_put_short(msg, 0); /* ChannelSequence */
	msg_put_short(msg, 0); /* Reserved */
	msg_put_short(msg, SMB2_NEGO_CMD); /* Command */
	msg_put_short(msg, 31); /* CreditRequest */
	msg_put_int(msg, 0); /* Flags */
	msg_put_int(msg, 0); /* NextCommand */
	msg_put_int(msg, 0); /* MessageId1 */
	msg_put_int(msg, 0); /* MessageId2 */
	msg_put_int(msg, 0); /* Reserved */
	msg_put_int(msg, 0); /* TreeId */
	msg_put_int(msg, 0); /* SessionId1 */
	msg_put_int(msg, 0); /* SessionId2 */
	msg_put_int(msg, 0); /* Signature1 */
	msg_put_int(msg, 0); /* Signature2 */
	msg_put_int(msg, 0); /* Signature3 */
	msg_put_int(msg, 0); /* Signature4 */
}

void
msg_put_nego_body(msg_buf_t *msg, uint16_t num_ctx)
{
	msg_put_short(msg, SMB2_NEGO_REQ_STRUCT_SIZE); /* StructureSize */
	msg_put_short(msg, 1); /* One DialectCount */
	msg_put_short(msg, 1); /* SecurityMode - enabled */
	msg_put_short(msg, 0); /* Reserved */
	msg_put_int(msg, SMB2_GLOBAL_CAP_ENCRYPTION); /* Capabilities */

	msg_put_int(msg, 0xDEADBEEF); /* GUID1-4 */
	msg_put_int(msg, 0xDEADBEEF); /* GUID5-8 */
	msg_put_int(msg, 0xDEADBEEF); /* GUID9-12 */
	msg_put_int(msg, 0xDEADBEEF); /* GUID13-16 */

	msg_put_int(msg, NEG_CTX_OFFSET); /* NegotiateContextOffset */
	msg_put_short(msg, num_ctx); /* NegotiateContextCount */
	msg_put_short(msg, 0); /* Reserved2 */

	msg_put_short(msg, SMB3_PROTO_311); /* Dialects */
	msg_align(msg, 8); /* padding */
}

void
msg_put_preauth_ctx(msg_buf_t *msg, uint16_t *preauth, ulong_t num_auth_alg)
{
	int i;

	/* Preauth Negotiation Context */
	msg_put_short(msg, SMB2_PREAUTH_CAPS); /* ContextType */
	msg_put_short(msg, PREAUTH_HDR + num_auth_alg * 2); /* DataLength */
	msg_put_int(msg, 0); /* Reserved */

	/* Context Data */
	msg_put_short(msg, num_auth_alg); /* HashAlgorithmCount */
	msg_put_short(msg, NEG_SALT_LEN); /* SaltLength */
	for (i = 0; i < num_auth_alg; i++) /* HashAlgorithms */
		msg_put_short(msg, preauth[i]);
	msg_put_int(msg, 0xBADDCAFE); /* SALT1-4 */
	msg_put_int(msg, 0xBADDCAFE); /* SALT5-8 */
	msg_put_int(msg, 0xBADDCAFE); /* SALT9-12 */
	msg_put_int(msg, 0xBADDCAFE); /* SALT13-16 */
	msg_put_int(msg, 0xBADDCAFE); /* SALT17-20 */
	msg_put_int(msg, 0xBADDCAFE); /* SALT21-24 */
	msg_put_int(msg, 0xBADDCAFE); /* SALT25-28 */
	msg_put_int(msg, 0xBADDCAFE); /* SALT29-32 */
}

void
msg_put_encrypt_ctx(msg_buf_t *msg, uint16_t *encrypt, ulong_t num_enc_alg)
{
	int i;

	/* Encryption Negotiation Context */
	msg_put_short(msg, SMB2_ENCRYPTION_CAPS); /* ContextType */
	msg_put_short(msg, ENCRYPT_HDR + num_enc_alg * 2); /* DataLength */
	msg_put_int(msg, 0); /* Reserved */

	/* Context Data */
	msg_put_short(msg, num_enc_alg); /* CipherCount */
	for (i = 0; i < num_enc_alg; i++) /* Ciphers */
		msg_put_short(msg, encrypt[i]);
}

uint16_t
msg_get_short(msg_buf_t *msg)
{
	uint16_t val;

	if ((msg->len - msg->offset) < sizeof (val)) {
		fprintf(stderr, "no room: size %d remaining %d\n",
		    sizeof (val), msg->len - msg->offset);
		exit(10);
	}

	val = LE_IN16(msg->buf + msg->offset);
	msg->offset += sizeof (val);
	return (val);
}

uint32_t
msg_get_int(msg_buf_t *msg)
{
	uint32_t val;

	if ((msg->len - msg->offset) < sizeof (val)) {
		fprintf(stderr, "no room: size %d remaining %d\n",
		    sizeof (val), msg->len - msg->offset);
		exit(10);
	}

	val = LE_IN32(msg->buf + msg->offset);
	msg->offset += sizeof (val);
	return (val);
}

void
msg_check_preauth(msg_buf_t *msg)
{
	uint16_t cnt = msg_get_short(msg); /* HashAlgorithmCount */
	uint16_t alg;

	if (cnt != 1) {
		fprintf(stderr, "preauth: bad count: %d\n", cnt);
		exit(15);
	}

	(void) msg_get_short(msg); /* SaltLength */
	alg = msg_get_short(msg);
	printf("preauth alg: %#x\n", alg);

	/* ignore Salt */
}

void
msg_check_encrypt(msg_buf_t *msg)
{
	uint16_t cnt = msg_get_short(msg); /* CipherCount */
	uint16_t alg;

	if (cnt != 1) {
		fprintf(stderr, "encrypt: bad count: %d\n", cnt);
		exit(15);
	}

	alg = msg_get_short(msg);
	printf("encrypt alg: %#x\n", alg);
}

void
msg_put_nbhdr(msg_buf_t *msg)
{
	uint16_t off = msg->offset;

	msg->buf -= 4;
	msg->len += 4;

	msg->buf[0] = 0;
	msg->buf[1] = 0;
	msg->buf[2] = (char)(off >> 8);
	msg->buf[3] = (char)off;
	msg->offset += 4;
}

void
msg_skip_nbhdr(msg_buf_t *msg)
{
	msg->buf += 4;
	msg->len -= 4;
}

int
main(int argc, char *argv[])
{
	char outbuf[1024];
	msg_buf_t msg = {
		.buf = outbuf,
		.len = sizeof (outbuf)
	};
	struct sockaddr_in addr = {0};
	uint16_t *preauth = NULL, *encrypt = NULL;
	ssize_t inlen, offset;
	ulong_t num_enc_alg, num_auth_alg;
	int fd, i;
	uint16_t num_ctx = 0;
	uint32_t status;

	if (argc < 4) {
		fprintf(stderr, "usage: %s <hostname or ip> "
		    "<encrypt cipher count> <preauth cipher count> "
		    "[encrypt cipher]... [preauth cipher]...\n", argv[0]);
		exit(1);
	}

	errno = 0;
	num_enc_alg = strtoul(argv[2], NULL, 0);
	if (errno != 0) {
		perror("Bad encryption cipher count value");
		exit(2);
	}

	errno = 0;
	num_auth_alg = strtoul(argv[3], NULL, 0);
	if (errno != 0) {
		perror("Bad preauth cipher count value");
		exit(3);
	}

	if (argc < (4 + num_enc_alg + num_auth_alg)) {
		fprintf(stderr, "not enough algorithms\n");
		fprintf(stderr, "usage: %s <hostname or ip> "
		    "<encrypt cipher count> <preauth cipher count> "
		    "[encrypt cipher]... [preauth cipher]...\n", argv[0]);
		exit(1);
	}

	if (num_enc_alg > 0) {
		num_ctx++;
		encrypt = malloc(num_enc_alg * sizeof (*encrypt));
		if (encrypt == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(5);
		}
		for (i = 0; i < num_enc_alg; i++) {
			ulong_t val;

			errno = 0;
			val = strtoul(argv[4 + i], NULL, 0);
			if (errno != 0) {
				perror("Bad encryption cipher value");
				exit(3);
			}
			if (val > UINT16_MAX) {
				fprintf(stderr,
				    "encryption cipher value too big\n");
				exit(4);
			}

			encrypt[i] = (uint16_t)val;
		}
	}
	if (num_auth_alg > 0) {
		num_ctx++;
		preauth = malloc(num_auth_alg * sizeof (*preauth));
		if (preauth == NULL) {
			fprintf(stderr, "out of memory\n");
			exit(5);
		}
		for (i = 0; i < num_auth_alg; i++) {
			ulong_t val;

			errno = 0;
			val = strtoul(argv[4 + num_enc_alg + i], NULL, 0);
			if (errno != 0) {
				perror("Bad preauth cipher value");
				exit(3);
			}
			if (val > UINT16_MAX) {
				fprintf(stderr,
				    "preauth cipher value too big\n");
				exit(4);
			}

			preauth[i] = (uint16_t)val;
		}
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);

	if (isdigit(argv[1][0]) != 0) {
		if (inet_aton(argv[1], &addr.sin_addr) == 0) {
			perror("inet_aton failed:");
			exit(4);
		}
	} else {
		struct hostent *ent = gethostbyname(argv[1]);
		if (ent == NULL) {
			perror("gethostbyname failed:");
			exit(5);
		}

		addr.sin_addr = *(struct in_addr *)ent->h_addr_list[0];
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(IPPORT_SMB);

	msg_put_hdr(&msg);
	msg_put_nego_body(&msg, num_ctx);

	if (num_auth_alg > 0) {
		msg_put_preauth_ctx(&msg, preauth, num_auth_alg);
		if (--num_ctx > 0)
			msg_align(&msg, 8); /* padding */
	}

	if (num_enc_alg > 0) {
		msg_put_encrypt_ctx(&msg, encrypt, num_enc_alg);
		if (--num_ctx > 0)
			msg_align(&msg, 8); /* padding */
	}

	msg_put_nbhdr(&msg);
	if (connect(fd, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
		perror("connect failed:\n");
		exit(6);
	}

	if (send(fd, outbuf, msg.offset, 0) < 0) {
		perror("send failed:\n");
		exit(7);
	}

	inlen = read(fd, outbuf, 4);
	if (inlen < 4) {
		fprintf(stderr, "response short: %d\n", inlen);
		exit(8);
	}
	msg.len = (outbuf[1] << 16) | (outbuf[2] << 8) | outbuf[3];
	inlen = read(fd, outbuf, msg.len);
	(void) close(fd);

	msg.buf = outbuf;
	msg.len = inlen;

	if (msg.len < SMB2_HDR_LEN) {
		fprintf(stderr, "response short: %d\n", inlen);
		exit(8);
	}

	msg.offset = RESP_HDR_STATUS_OFFSET;
	status = msg_get_int(&msg);
	printf("status: %#x\n", status);
	if (status != 0) {
		fprintf(stderr, "error status\n");
		exit(12);
	}

	if (msg.len < NEG_RESP_OFFSET_OFFSET) {
		fprintf(stderr, "response short: %d\n", msg.len);
		exit(8);
	}

	/* Skip to NegotiateContextCount */
	msg.offset = NEG_RESP_COUNT_OFFSET;
	num_ctx = msg_get_short(&msg);

	/* Skip to NegotiateContextOffset */
	msg.offset = NEG_RESP_OFFSET_OFFSET;
	offset = msg_get_short(&msg);

	if (offset < NEG_RESP_OFFSET_OFFSET || offset > msg.len) {
		fprintf(stderr, "bad offset: %d (%#x)\n", offset, offset);
		exit(9);
	}

	msg.offset = offset;
	for (i = 0; i < num_ctx; i++) {
		size_t saved_off;
		uint16_t type = msg_get_short(&msg); /* ContextType */
		uint16_t len = msg_get_short(&msg); /* DataLength */
		(void) msg_get_int(&msg); /* Reserved */

		saved_off = msg.offset;
		switch (type) {
		case SMB2_PREAUTH_CAPS:
			msg_check_preauth(&msg);
			break;
		case SMB2_ENCRYPTION_CAPS:
			msg_check_encrypt(&msg);
			break;
		default:
			break;
		}

		msg.offset = saved_off + len;
		if (i != (num_ctx - 1))
			msg_align(&msg, 8);
	}

	return (0);
}
