
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
#include "test_util.h"
#include "clnt_pipe.h"

extern struct mslm_NetShareInfo_1 shares202[202];

extern unsigned char send_data[];
extern unsigned char recv_data[];
extern unsigned int send_total;
extern unsigned int recv_total;

int share_enum_rpc(void *ctx, char *server);
void view_print_share(char *share, int type, char *comment);

struct test_buf rbuf, wbuf;
static unsigned char test_data[65536];

static int
cmp_u8(uint8_t *s1, uint8_t *s2)
{
	return (strcmp((char *)s1, (char *)s2));
}

/*
 * Create an RPC pipe object (np)
 * Call share enumeration client stub,
 * compare outputs.
 */
int
main(int argc, char **argv)
{
	struct pipe_private pp;

	wbuf.buf = test_data;
	wbuf.max = sizeof (test_data);
	wbuf.off = 0;

	rbuf.buf = recv_data;
	rbuf.max = recv_total;
	rbuf.off = 0;

	pp.r = &rbuf;
	pp.w = &wbuf;

	/*
	 * Run the RPC client call etc.
	 */
	(void) share_enum_rpc(&pp, "localhost");
	
	/*
	 * Compare the outputs...
	 */
	if (wbuf.off != send_total) {
		printf("wrong output len %d, expected %d\n",
		    wbuf.off, send_total);
	}
	if (compare(test_data, send_data, wbuf.off)) {
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
	struct mslm_NetShareInfo_1 *nsi;
	struct mslm_NetShareInfo_1 *csi;
	int i, count, errcnt;
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

	/* Check the share list */
	count = res.bufptr1->entriesread;
	errcnt = 0;
	if (count != 202) {
		printf("wrong share count %d, expected %d\n",
		    count, 202);
		errcnt++;
	}
	i = 0, nsi = res.bufptr1->entries;
	while (i < count) {
		boolean_t wrong = B_FALSE;

		/* canned share info */
		csi = &shares202[i];
		if (csi->shi1_type != nsi->shi1_type) {
			printf("wrong share type at %d\n", i);
			wrong = B_TRUE;
		}
		if (cmp_u8(csi->shi1_netname, nsi->shi1_netname)) {
			printf("wrong share name at %d\n", i);
			wrong = B_TRUE;
		}
		if (cmp_u8(csi->shi1_comment, nsi->shi1_comment)) {
			printf("wrong share comment at %d\n", i);
			wrong = B_TRUE;
		}

		if (wrong) {
			printf("Expected share:\n");
			view_print_share((char *)csi->shi1_netname,
			    csi->shi1_type, (char *)csi->shi1_comment);
			printf("Received share:\n");
			view_print_share((char *)nsi->shi1_netname,
			    nsi->shi1_type, (char *)nsi->shi1_comment);
			errcnt++;
		}
		i++, nsi++;
	}

	if (errcnt > 0) {
		/* Print the share list. */
		count = res.bufptr1->entriesread;
		if (count == 0)
			printf("(no shares)\n");
		else
			view_print_share(NULL, 0, NULL);
		i = 0, nsi = res.bufptr1->entries;
		while (i < count) {
			/* Convert UTF-8 to local code set? */
			view_print_share((char *)nsi->shi1_netname,
			    nsi->shi1_type, (char *)nsi->shi1_comment);
			i++, nsi++;
		}
	} else {
		printf("Share list correct\n");
	}

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

int debug_ndo = 0;
void
ndo_trace(const char *s)
{
	if (debug_ndo)
		printf("NDOTRACE: %s\n", s);
}
