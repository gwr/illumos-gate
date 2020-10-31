
/*
 * Simulate RPC client-side call processing of share enumeration
 */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libmlrpc/libmlrpc.h>
#include "srvsvc1_clnt.h"

#include "srvsvc1_data.h"
#include "test_util.h"
#include "clnt_pipe.h"

int share_enum_rpc(void *ctx, char *server);
void view_print_share(char *share, int type, char *comment);

static unsigned char test_data[1000];

struct test_buf wbuf = {
	.buf = test_data,
	.max = sizeof (test_data),
	.off = 0
};

struct test_buf rbuf = {
	.buf = reply_data,
	.max = sizeof (reply_data),
	.off = 0
};

/*
 * Create an RPC pipe object (np)
 * Call share enumeration client stub,
 * compare outputs.
 */
int
main(int argc, char **argv)
{
	struct pipe_private pp;

	pp.r = &rbuf;
	pp.w = &wbuf;

	/*
	 * Run the RPC client call etc.
	 */
	(void) share_enum_rpc(&pp, "gwr153ns5");
	
	/*
	 * Compare the outputs...
	 */
	if (wbuf.off != sizeof (call_data)) {
		printf("wrong output len %d, expected %d\n",
		    wbuf.off, sizeof (call_data));
	}
	if (compare(test_data, call_data, wbuf.off)) {
		printf("wrong output data\n");
		/* hexdump(test_data, wbuf.off); */
		return (1);
	}

	return (0);
}

int
share_enum_rpc(void *ctx, char *server)
{
	mlrpc_handle_t handle;
	ndr_service_t *svc;
	union mslm_NetShareEnum_ru res;
	struct mslm_NetShareInfo_1 *nsi1;
	int i, count;
	int err;

	/*
	 * Create an RPC handle using the smb_ctx we already have.
	 * Just local allocation and initialization.
	 */
	srvsvc1_initialize();
	svc = ndr_svc_lookup_name("srvsvc");
	if (svc == NULL)
		return (ENOENT);

	err = mlrpc_clh_create(&handle, ctx);
	if (err)
		return (err);

	/*
	 * Try to bind to the RPC service.  If it fails,
	 * just return the error and the caller will
	 * fall back to RAP.
	 */
	err = mlrpc_clh_bind(&handle, svc);
	if (err)
		goto out;

	err = srvsvc_net_share_enum(&handle, server, 1, &res);
	if (err)
		goto out;

#if 1
	/* Print the share list. */
	count = res.bufptr1->entriesread;
	if (count == 0)
		printf("(no shares)\n");
	else
		view_print_share(NULL, 0, NULL);
	i = 0, nsi1 = res.bufptr1->entries;
	while (i < count) {
		/* Convert UTF-8 to local code set? */
		view_print_share((char *)nsi1->shi1_netname,
		    nsi1->shi1_type, (char *)nsi1->shi1_comment);
		i++, nsi1++;
	}
#endif

out:
	(void) mlrpc_clh_free(&handle);
	return (err);
}

/*
 * Print one line of the share list, or
 * if SHARE is null, print the header line.
 */
void
view_print_share(char *share, int type, char *comment)
{

	if (share == NULL) {
		printf("Share        Type       Comment\n");
		printf("-------------------------------\n");
		return;
	}

	if (comment == NULL)
		comment = "";

	printf("%-12s %-10d %s\n", share, type, comment);
}
