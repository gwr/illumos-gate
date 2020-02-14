
#include <libmlrpc/libmlrpc.h>

struct test_buf {
	unsigned char *buf;
	size_t max;
	size_t off;
};

struct pipe_private {
	struct test_buf *r;
	struct test_buf *w;
};

ndr_pipe_t * test_np_new(char *epname,
    struct test_buf *r, struct test_buf *w);
