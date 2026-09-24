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
 * Copyright 2026 RackTop Systems Inc.
 */

#include <sys/byteorder.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/netrauth.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define	CFG_DIR		"/opt/libmlrpc-tests/cfg"
#define	CFG_FILE	CFG_DIR "/samlogon.config"
#define	CHAL_FILE	CFG_DIR "/challenge.bin"
#define	NT_FILE		CFG_DIR "/nt_response.bin"
#define	LM_FILE		CFG_DIR "/lm_response.bin"
#define	BLOB_MAX	2048
#define	BLOB_HEADER_LEN	28

static int
append_pair(uchar_t *blob, size_t *len, uint16_t type,
    const void *value, size_t value_len)
{
	if (value_len > UINT16_MAX || *len > BLOB_MAX ||
	    BLOB_MAX - *len < 4 || value_len > BLOB_MAX - *len - 4)
		return (-1);

	LE_OUT16(blob + *len, type);
	LE_OUT16(blob + *len + 2, value_len);
	if (value_len != 0)
		bcopy(value, blob + *len + 4, value_len);
	*len += 4 + value_len;
	return (0);
}

static int
append_name(uchar_t *blob, size_t *len, uint16_t type, const char *name)
{
	smb_wchar_t unicode[MAXHOSTNAMELEN + 6];
	size_t nchars = strlen(name);
	int nbytes;

	if (nchars == 0 || nchars >= sizeof (unicode) / sizeof (unicode[0]))
		return (-1);

	nbytes = smb_auth_qnd_unicode(unicode, name, nchars);
	if (nbytes <= 0)
		return (-1);

	return (append_pair(blob, len, type, unicode, nbytes));
}

static int
write_binary(const char *path, const uchar_t *data, size_t len)
{
	size_t done = 0;
	ssize_t n;
	int fd;
	int error = 0;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		error = errno;
		goto out;
	}
	if (fchmod(fd, 0600) != 0) {
		error = errno;
		goto closeout;
	}

	while (done < len) {
		n = write(fd, data + done, len - done);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			error = n < 0 ? errno : EIO;
			goto closeout;
		}
		done += n;
	}

closeout:
	if (close(fd) != 0 && error == 0)
		error = errno;
out:
	if (error != 0)
		(void) fprintf(stderr, "%s: %s\n", path, strerror(error));
	return (error != 0 ? -1 : 0);
}

static int
write_var(FILE *fp, const char *key, const char *value)
{
	const char *p;

	if (fprintf(fp, "export %s='", key) < 0)
		return (-1);
	for (p = value; *p != '\0'; p++) {
		if (*p == '\n' || *p == '\r')
			return (-1);
		if (*p == '\'') {
			if (fputs("'\\''", fp) == EOF)
				return (-1);
		} else if (fputc(*p, fp) == EOF) {
			return (-1);
		}
	}
	return (fputs("'\n", fp) == EOF ? -1 : 0);
}

static int
write_config(const char *domain, const char *dc, const char *username,
    const char *host)
{
	FILE *fp;
	int fd;
	int rc = -1;

	fd = open(CFG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		goto out;
	if (fchmod(fd, 0600) != 0) {
		(void) close(fd);
		goto out;
	}
	fp = fdopen(fd, "w");
	if (fp == NULL) {
		(void) close(fd);
		goto out;
	}

	if (write_var(fp, "NETBIOS_DOMAIN", domain) == 0 &&
	    write_var(fp, "DC_FQDN", dc) == 0 &&
	    write_var(fp, "USERNAME", username) == 0 &&
	    write_var(fp, "CLIENT_COMPUTER", host) == 0 &&
	    write_var(fp, "CHALLENGE_FILE", CHAL_FILE) == 0 &&
	    write_var(fp, "NT_RESPONSE_FILE", NT_FILE) == 0 &&
	    write_var(fp, "LM_RESPONSE_FILE", LM_FILE) == 0 &&
	    fflush(fp) == 0)
		rc = 0;
	if (fclose(fp) != 0)
		rc = -1;
	if (rc != 0)
		(void) unlink(CFG_FILE);
out:
	if (rc != 0)
		(void) fprintf(stderr, "%s: cannot write configuration\n",
		    CFG_FILE);
	return (rc);
}

int
main(int argc, char *argv[])
{
	char nbhost[NETBIOS_NAME_SZ];
	char domain[NETBIOS_NAME_SZ];
	char fqhost[MAXHOSTNAMELEN];
	char fqdomain[MAXHOSTNAMELEN];
	char dc[MAXHOSTNAMELEN];
	char target[MAXHOSTNAMELEN + 6];
	char *username = NULL;
	char *password = NULL;
	smb_inaddr_t dcaddr;
	struct timeval now;
	uint64_t nt_time;
	uchar_t challenge[SMBAUTH_CHAL_SZ];
	uchar_t nt_hash[SMBAUTH_HASH_SZ] = { 0 };
	uchar_t v2_hash[SMBAUTH_HASH_SZ] = { 0 };
	uchar_t blob[BLOB_MAX] = { 1, 1 };
	uchar_t input[SMBAUTH_CHAL_SZ + BLOB_MAX];
	uchar_t nt_response[SMBAUTH_HASH_SZ + BLOB_MAX];
	uchar_t lm_response[SMBAUTH_LM_RESP_SZ] = { 0 };
	uchar_t bindings[SMBAUTH_HASH_SZ] = { 0 };
	size_t blob_len = BLOB_HEADER_LEN;
	int rc = EXIT_FAILURE;
	int target_len;

	if (argc != 2 || argv[1][0] == '\0' ||
	    strpbrk(argv[1], "\r\n") != NULL) {
		(void) fprintf(stderr,
		    "usage: %s <username - no domain component>\n", argv[0]);
		return (EXIT_FAILURE);
	}
	if (strpbrk(argv[1], "\\/@") != NULL) {
		(void) fprintf(stderr,
		    "username must not include a domain component\n");
		return (EXIT_FAILURE);
	}
	if (smb_getnetbiosname(nbhost, sizeof (nbhost)) != 0 ||
	    smb_getdomainname(domain, sizeof (domain)) != 0 ||
	    smb_getfqhostname(fqhost, sizeof (fqhost)) != 0 ||
	    smb_getfqdomainname(fqdomain, sizeof (fqdomain)) != 0 ||
	    smb_get_dcinfo(dc, sizeof (dc), &dcaddr) != NT_STATUS_SUCCESS ||
	    dc[0] == '\0') {
		(void) fprintf(stderr,
		    "cannot read SMB domain and DC settings\n");
		goto out;
	}
	target_len = snprintf(target, sizeof (target), "cifs/%s", fqhost);
	if (target_len < 0 || (size_t)target_len >= sizeof (target)) {
		(void) fprintf(stderr, "SMB host name is too long\n");
		goto out;
	}

	username = strdup(argv[1]);
	if (username == NULL) {
		perror("strdup");
		goto out;
	}
	password = getpassphrase("Domain password: ");
	if (password == NULL || password[0] == '\0') {
		(void) fprintf(stderr, "missing domain password\n");
		goto out;
	}
	if (smb_auth_ntlm_hash(password, nt_hash) != SMBAUTH_SUCCESS ||
	    smb_auth_ntlmv2_hash(nt_hash, username, domain, v2_hash) !=
	    SMBAUTH_SUCCESS) {
		(void) fprintf(stderr, "cannot derive NTLMv2 key\n");
		goto out;
	}
	explicit_bzero(password, strlen(password));
	password = NULL;

	if (gettimeofday(&now, NULL) != 0) {
		perror("gettimeofday");
		goto out;
	}
	nt_time = ((uint64_t)now.tv_sec + 11644473600ULL) * 10000000ULL +
	    (uint64_t)now.tv_usec * 10;
	LE_OUT64(blob + 8, nt_time);
	arc4random_buf(challenge, sizeof (challenge));
	arc4random_buf(blob + 16, SMBAUTH_CHAL_SZ);

	if (append_name(blob, &blob_len, 1, nbhost) != 0 ||
	    append_name(blob, &blob_len, 2, domain) != 0 ||
	    append_name(blob, &blob_len, 3, fqhost) != 0 ||
	    append_name(blob, &blob_len, 4, fqdomain) != 0 ||
	    append_pair(blob, &blob_len, 0x0a, bindings,
	    sizeof (bindings)) != 0 ||
	    append_name(blob, &blob_len, 9, target) != 0 ||
	    append_pair(blob, &blob_len, 0, NULL, 0) != 0) {
		(void) fprintf(stderr, "cannot construct NTLMv2 target info\n");
		goto out;
	}
	if (BLOB_MAX - blob_len < 4) {
		(void) fprintf(stderr, "NTLMv2 blob is too large\n");
		goto out;
	}
	blob_len += 4;	/* Reserved bytes after the target-info list */

	bcopy(challenge, input, sizeof (challenge));
	bcopy(blob, input + sizeof (challenge), blob_len);
	if (smb_auth_hmac_md5(input, sizeof (challenge) + blob_len,
	    v2_hash, sizeof (v2_hash), nt_response) != SMBAUTH_SUCCESS) {
		(void) fprintf(stderr, "cannot compute NTLMv2 response\n");
		goto out;
	}
	bcopy(blob, nt_response + SMBAUTH_HASH_SZ, blob_len);

	if (write_binary(CHAL_FILE, challenge, sizeof (challenge)) != 0 ||
	    write_binary(LM_FILE, lm_response, sizeof (lm_response)) != 0 ||
	    write_binary(NT_FILE, nt_response,
	    SMBAUTH_HASH_SZ + blob_len) != 0 ||
	    write_config(domain, dc, argv[1], nbhost) != 0)
		goto out;
	(void) printf("Wrote %s and NTLMv2 test responses\n", CFG_FILE);
	rc = EXIT_SUCCESS;

out:
	if (password != NULL)
		explicit_bzero(password, strlen(password));
	explicit_bzero(nt_hash, sizeof (nt_hash));
	explicit_bzero(v2_hash, sizeof (v2_hash));
	explicit_bzero(input, sizeof (input));
	free(username);
	return (rc);
}
