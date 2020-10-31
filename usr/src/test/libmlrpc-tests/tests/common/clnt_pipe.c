
#include <stdio.h>
#include <strings.h>
#include <sys/errno.h>

#include "clnt_pipe.h"

/*
 * Only support one open pipe here.
 */
static struct pipe_private *one_pp;

int
smb_fh_open(void *ctx, const char *path, int oflag)
{
	one_pp = ctx;
	return (0);
}

int
smb_fh_close(int fd)
{
	return (0);
}

/*
 * Cheating a little: Looking at the RPC frag length to
 * arrange for read to return only the current frag,
 * instead of whatever will fit in the caller's len.
 */
int
smb_fh_read(int fd, off64_t offset, size_t len,
	char *dst)
{
	struct test_buf *tb = one_pp->r;
	unsigned char *p = tb->buf + tb->off;

	if (tb->eom == 0 && ((tb->off + 10) <= tb->max)) {
		tb->eom = p[8] + (p[9] << 8);
	}

	if (len > tb->eom)
		len = tb->eom;

	if ((tb->off + len) > tb->max)
		len = tb->max - tb->off;
	if (len == 0)
		return (0); // end of data
		
	/* "Recv" copies pipe private to buf arg */
	memcpy(dst, tb->buf + tb->off, len);
	tb->off += len;
	tb->eom -= len;

	return (len);
}

int
smb_fh_write(int fd, off64_t offset, size_t len,
	const char *src)
{
	struct test_buf *tb = one_pp->w;

	if ((tb->off + len) > tb->max)
		len = tb->max - tb->off;
	if (len == 0)
		return (0); // end of data

	/* "Write" copies buf arg to into pipe private */
	memcpy(tb->buf + tb->off, src, len);
	tb->off += len;

	return (len);
}

int
smb_fh_xactnp(int fd,
	int tdlen, const char *tdata,	/* transmit */
	int *rdlen, char *rdata,	/* receive */
	int *more)
{
	int tlen, rlen;

	tlen = smb_fh_write(fd, 0, tdlen, tdata);
	if (tlen < 0)
		return (-1);

	rlen = smb_fh_read(fd, 0, *rdlen, rdata);
	if (rlen < 0)
		return (-1);

	*rdlen = rlen;
	*more = 0;
	
	return (0);
}
