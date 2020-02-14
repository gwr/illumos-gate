
#include <stdio.h>
#include <strings.h>
#include <sys/errno.h>

#include "test_pipe.h"

uint16_t pipe_max_msgsize = 4280;  // SMB_PIPE_MAX_MSGSIZE;

static int pipe_send(ndr_pipe_t *, void *, size_t);
static int pipe_recv(ndr_pipe_t *, void *, size_t);

struct pipe_private {
	struct test_buf *r;
	struct test_buf *w;
};

ndr_pipe_t *
test_np_new(char *epname, struct test_buf *r, struct test_buf *w)
{
	ndr_pipe_t *np;
	struct pipe_private *pp;
	size_t len;

	len = sizeof (*np) + sizeof (*pp);
	np = malloc(len);
	if (np == NULL)
		return (NULL);
	pp = (void*)(np + 1);

	bzero(np, len);
	np->np_listener = NULL;
	np->np_endpoint = epname;
	np->np_user = pp;
	np->np_send = pipe_send;
	np->np_recv = pipe_recv;
	np->np_fid = -1;
	np->np_max_xmit_frag = pipe_max_msgsize;
	np->np_max_recv_frag = pipe_max_msgsize;

	pp->r = r;
	pp->w = w;

	return (np);
}

/*
 * These are the transport get/put callback functions provided
 * via the ndr_pipe_t object to the libmlrpc`ndr_pipe_worker.
 * These are called only with known PDU sizes and should
 * loop as needed to transfer the entire message.
 * These return zero or errno.
 *
 * In this test harness, pipe_recv gets the next chunk of "canned"
 * request data for this RPC service, and pipe_send compares the
 * output to the next chunk of "canned" output.
 */
static int
pipe_recv(ndr_pipe_t *np, void *buf, size_t len)
{
	struct pipe_private *pp = np->np_user;

	if ((pp->r->off + len) > pp->r->max)
		len = pp->r->max - pp->r->off;
	if (len == 0)
		return (EIO); // end of data
		
	/* "Recv" copies pipe private to buf arg */
	memcpy(buf, pp->r->buf + pp->r->off, len);
	pp->r->off += len;

	return (0);
}

static int
pipe_send(ndr_pipe_t *np, void *buf, size_t len)
{
	struct pipe_private *pp = np->np_user;

	if ((pp->w->off + len) > pp->w->max)
		len = pp->w->max - pp->w->off;
	if (len == 0)
		return (EIO); // end of data

	/* "Send" copies buf arg to into pipe private */
	memcpy(pp->w->buf + pp->w->off, buf, len);
	pp->w->off += len;

	return (0);
}

void
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
			putchar(' ');
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
				putchar(' ');
			printf("   ");
			idx++;
		}
		printf("%s\n", ascii);
	}
}

int
compare(uchar_t *p1, uchar_t *p2, size_t len)
{
	int off, row;
	int diffs = 0;

	for (off = 0; off < len; off++) {
		if (p1[off] != p2[off]) {
			printf("differs at off %d\n", off);
			row = off & ~15;
			hexdump(p1+row, 16);
			hexdump(p2+row, 16);
			off = row + 16;
			diffs++;
		}
	}

	return (diffs);
}
