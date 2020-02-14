
/*
 * Simulate RPC server-side call processing of share enumeration
 */

#include <stdio.h>

#include "srvsvc1_data.h"
#include "test_util.h"
#include "srv_pipe.h"

void srvsvc_initialize(void);

static unsigned char test_data[1000];

struct test_buf rbuf = {
	.buf = call_data,
	.max = sizeof (call_data),
	.off = 0
};

struct test_buf wbuf = {
	.buf = test_data,
	.max = sizeof (test_data),
	.off = 0
};

/*
 * Create an RPC pipe object (np)
 * Call ndr_pipe_worker(np)
 */
int
main(int argc, char **argv)
{
	ndr_pipe_t *np;

	srvsvc_initialize();

	np = test_np_new("srvsvc", &rbuf, &wbuf);
	if (np == NULL) {
		printf("test_np_new failed\n");
		return (1);
	}

	/*
	 * Run the RPC service loop worker, which
	 * returns when it sees the pipe close.
	 */
	ndr_pipe_worker(np);
	
	/*
	 * Compare the outputs...
	 */
	if (wbuf.off != sizeof (reply_data)) {
		printf("wrong output len %d, expected %d\n",
		    wbuf.off, sizeof (reply_data));
	}
	if (compare(test_data, reply_data, wbuf.off)) {
		printf("wrong output data\n");
		hexdump(test_data, wbuf.off);
	}

	return (0);
}
