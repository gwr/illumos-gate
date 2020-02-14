
#include <sys/types.h>

struct test_buf {
	unsigned char *buf;
	size_t max;
	size_t off;
	size_t eom;
};

struct pipe_private {
	struct test_buf *r;
	struct test_buf *w;
};

int  smb_fh_close(int);
int  smb_fh_open(void *ctx, const char *, int);
int  smb_fh_read(int, off64_t, size_t, char *);
int  smb_fh_write(int, off64_t, size_t, const char *);
int  smb_fh_xactnp(int, int, const char *,
	int *, char *, int *);
int  smb_fh_getssnkey(int, uchar_t *, size_t);

