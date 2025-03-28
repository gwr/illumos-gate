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
 * Copyright 2025 RackTop Systems, Inc.
 */

/*
 * Test & debug program for getdents_ex, extdirent.h etc.
 */

#define	_LARGEFILE64_SOURCE	1

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/extdirent.h>
#include <sys/dirent.h>
#include <sys/vnode.h>	/* V_RDDIR_ENTFLAGS */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <getdents_ex.h>

static void show_dents_ex(int, char *, int);

#define	TBUFSZ 8192
static char tbuf[TBUFSZ];

int
main(int argc, char **argv)
{
	char *rdir;
	int cnt;
	int dfd;
	int error;

	if (argc > 1)
		rdir = argv[1];
	else
		rdir = ".";

	dfd = open(rdir, O_RDONLY, 0);
	if (dfd < 0) {
		error = errno;
		fprintf(stderr, "open <%s> error=%d\n",	rdir, error);
		return (1);
	}

	do {
		cnt = getdents_ex(dfd, tbuf, TBUFSZ,
				V_RDDIR_ENTFLAGS);
		if (cnt < 0) {
			error = errno;
			fprintf(stderr, "getdents_ex %d\n", error);
			break;
		}
		show_dents_ex(dfd, tbuf, cnt);
	} while (cnt > 0);

	(void) close(dfd);
	return (0);
}

#if	S_IFMT != 0xF000
#error	"S_IFMT assumption"
#endif

/*
 * edirent_t ed_eflags values
 *
 * These are (apparently) the S_IFMT bits from sys/stat.h
 * Letters chosen are from "cmd/ls"
 */
static char ifmt[16] = {
	[S_IFIFO >> 12] = 'p',
	[S_IFCHR >> 12] = 'c',
	[S_IFDIR >> 12] = 'd',
	[S_IFBLK >> 12] = 'b',
	[S_IFREG >> 12] = '-',
	[S_IFLNK >> 12] = 'l',
	[S_IFSOCK >> 12] = 's',
	[S_IFDOOR >> 12] = 'D',
	[S_IFPORT >> 12] = 'P'
};

static void
show_dents_ex(int dfd, char *buf, int cnt)
{
	char *p;
	edirent_t *ed;
	char etype;

	p = buf;
	while (p < (buf + cnt)) {
		ed = (edirent_t *)(void *)p;
		p += ed->ed_reclen;

		/*
		 * Print etype, name
		 */
		etype = ifmt[ed->ed_eflags & 0xF];
		if (etype == '\0')
			etype = '?';

		printf("%c %s\n", etype, ed->ed_name);
	}
}
