
#include <libmlrpc/libmlrpc.h>

struct test_buf {
	unsigned char *buf;
	size_t max;
	size_t off;
};

ndr_pipe_t * test_np_new(char *epname,
    struct test_buf *r, struct test_buf *w);

int compare(uchar_t *p1, uchar_t *p2, size_t len);
void hexdump(const uchar_t *buf, int len);
