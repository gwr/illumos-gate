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
 * Test for smb server door access
 */

#include <sys/types.h>
#include <sys/debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <smbsrv/libsmb.h>
#include <smbsrv/smb_share.h>
#include <smbsrv/smb.h>

/*
 * Try both non-privileged and privileged operations on
 * the general purpose door service
 */
void
try_general(void)
{
	lsa_account_t act;
	int rc;

	/*
	 * Test something that does NOT require privilege.
	 * This assumes an account "test" exists.
	 */
	rc = smb_lookup_lname("test", SidTypeUnknown, &act);
	printf("smb_lookup_lname(test) ret %d (should work)\n", rc);

	/* Test something that DOES require privilege. */
	rc = smb_notify_dc_changed();
	if (rc == 0) {
		printf("smb_notify_dc_changed() allowed (bug)\n", rc);
	} else {
		printf("smb_notify_dc_changed() ret %d (OK)\n", rc);
	}
}

/*
 * Try both non-privileged and privileged operations on
 * the share door service
 */
void
try_shares(void)
{
	int n;
	uint32_t x;

	/* Test something that does NOT require privilege. */
	n = smb_share_count();
	printf("smb_share_count() ret %d (should work)\n", n);

	/* Test something that DOES require privilege. */
	x = smb_share_delete("no-such-share");
	if (x == NERR_InternalError) {
		printf("smb_share_delete() disallowed (OK)\n", x);
	} else if (x == NERR_NetNameNotFound) {
		printf("smb_share_delete() would be allowed (bug)\n");
	} else {
		printf("smb_share_delete() ret %d (unexpected)\n", x);
	}
}

/* quick and dirty -- from capture */
uchar_t userinfo[] = {
	'\x45', '\x50', '\x49', '\x50', '\x58', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x05',
	'\x00', '\x00', '\x00', '\x08', '\x00', '\x00', '\x00', '\x07',
	'\x4f', '\x49', '\x2d', '\x57', '\x4f', '\x52', '\x4b', '\x00',
	'\x00', '\x00', '\x00', '\x05', '\x00', '\x00', '\x00', '\x04',
	'\x74', '\x65', '\x73', '\x74', '\x00', '\x00', '\xea', '\x61',
	'\x00', '\x00', '\x00', '\x0a', '\x00', '\x00', '\x00', '\x09',
	'\x31', '\x32', '\x37', '\x2e', '\x30', '\x2e', '\x30', '\x2e',
	'\x31', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x02',
	'\x01', '\x00', '\x00', '\x7f', '\x00', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x63', '\x0a', '\x12', '\x31',
	'\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\xbb', '\xfa', '\xed', '\xfe', '\x21', '\x5e', '\x00', '\x00'
};

/*
 * If we're non-privileged, pipe open should set the ANON flag
 * on the service side, and (if debug) log our access.
 * Pass/fail status here is by observation of the log.
 */
void
try_pipe(void)
{
	struct sockaddr_un saddr;
	uint32_t status;
	int fd, n;

	bzero(&saddr, sizeof (saddr));
	saddr.sun_family = AF_UNIX;
	(void) snprintf(saddr.sun_path, sizeof (saddr.sun_path),
	    "%s/%s", SMB_PIPE_DIR, "srvsvc");

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("pipe: socket");
		return;
	}

	if (connect(fd, (struct sockaddr *)&saddr, sizeof (saddr)) != 0) {
		perror("pipe: connect");
		close(fd);
		return;
	}

	n = send(fd, userinfo, sizeof (userinfo), 0);
	if (n != sizeof (userinfo)) {
		if (n < 0)
			perror("pipe: send");
		else
			fprintf(stderr, "pipe: send len = %d\n", n);
		close(fd);
		return;
	}

	n = recv(fd, &status, sizeof (status), 0);
	if (n != sizeof (status)) {
		if (n < 0)
			perror("pipe: recv");
		else
			fprintf(stderr, "pipe: recv len = %d (?)\n", n);
	} else {
		printf("pipe: status = 0x%x (check svc log)\n", status);
	}

	/*
	 * Should see something like this in the svc log
	 * smbd: pipesvc: non-privileged client PID = 100660
	 */

	close(fd);
}

/* quick and dirty -- from capture: do_clinfo */
uchar_t authinfo[] = {
	'\x06', '\x00', '\x00', '\x00', '\x24', '\x00', '\x00', '\x00',
	'\x7f', '\x00', '\x00', '\x01', '\x00', '\x00', '\x00', '\x00',
	'\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
	'\x02', '\x00', '\x00', '\x00', '\x23', '\x70', '\x83', '\x8d',
	'\xdd', '\xdd', '\x35', '\xf9', '\x40', '\xba', '\x36', '\x08',
	'\x40', '\xba', '\x36', '\x08'
};

/*
 * If we're non-privileged, authsock open should hangup,
 * and (if debug) log our access.
 *
 * Pass/fail status here is by observation of the log.
 */
void
try_authsock(void)
{
	struct sockaddr_un saddr;
	smb_lsa_msg_hdr_t hdr;
	int fd, n;

	bzero(&saddr, sizeof (saddr));
	saddr.sun_family = AF_UNIX;
	(void) snprintf(saddr.sun_path, sizeof (saddr.sun_path),
	    "%s", SMB_AUTHSVC_SOCKNAME);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("auth: socket");
		return;
	}

	if (connect(fd, (struct sockaddr *)&saddr, sizeof (saddr)) != 0) {
		perror("auth: connect");
		close(fd);
		return;
	}

	n = send(fd, authinfo, sizeof (authinfo), 0);
	if (n != sizeof (authinfo)) {
		if (n < 0)
			perror("auth: send");
		else
			fprintf(stderr, "auth: send len = %d\n", n);
		close(fd);
		return;
	}

	n = recv(fd, &hdr, sizeof (hdr), 0);
	if (n != sizeof (hdr)) {
		if (n < 0)
			perror("auth: recv");
		if (n == 0)
			fprintf(stderr, "auth: hangup (OK)\n", n);
		else
			fprintf(stderr, "recv len = %d (bug?)\n", n);
		close(fd);
	} else {
		printf("hdr msgtype = %u, msglen = %u (got in!)\n",
		    hdr.lmh_msgtype, hdr.lmh_msglen);
	}

	/*
	 * Should see something like this in the svc log
	 * smbd: authsvc: non-privileged client PID = 100660
	 */

	close(fd);
}

int
main(int argc, char *argv[])
{

	try_general();
	try_shares();
	try_pipe();
	try_authsock();

	return (0);
}
