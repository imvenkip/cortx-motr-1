/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 02/22/2012
 */

#include "ioservice/ut/bulkio_common.h"
#include "lib/getopts.h"
#include "lib/thread.h"
#include "colibri/init.h"
#include <signal.h> 	/* sigaction */

extern int io_fop_dummy_fom_init(struct c2_fop *fop, struct c2_fom **m);
extern struct c2_fop_type_ops io_fop_rwv_ops;

bool stop_server = false;

static void bulkio_sigint_handler(int signum)
{
	stop_server = true;
}

static void print_stats(struct bulkio_params *bp)
{
}

int main(int argc, char *argv[])
{
	int			 rc;
	bool			 client = false;
	bool			 server = false;
	int			 port;
	const char		*caddr;
	const char		*saddr;
	struct bulkio_params	*bp;
	struct sigaction	 bulkio_int_action = {
		.sa_handler = bulkio_sigint_handler,
	};

	rc = sigaction(SIGINT, &bulkio_int_action, NULL);
	if (rc != 0) {
		fprintf(stderr, "Failed to set custom singal handler\
				for SIGINT.\n");
		return rc;
	}

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = C2_GETOPTS("bulkio", argc, argv,
			C2_FLAGARG('s', "Run server", &server),
			C2_FLAGARG('c', "Run client", &client),
			C2_STRINGARG('l', "Client addr",
				     LAMBDA(void, (const char *addr) {
				                   caddr = addr; } )),
	   		C2_STRINGARG('v', "Server addr",
				     LAMBDA(void, (const char *addr) {
				     		   saddr = addr;} )),
			C2_FORMATARG('p', "Port number", "%d", &port));

	if (rc != 0)
		return rc;

	/*
	 * Allocate and initialize struct bulkio_params first since
	 * it contains common things needed by client and server code.
	 */
	c2_addb_choose_default_level(AEL_NONE);
	C2_ALLOC_PTR(bp);
	C2_ASSERT(bp != NULL);
	bulkio_params_init(bp);

	if (server) {
		rc = bulkio_server_start(bp, saddr, port);
		if (rc != 0) {
			fprintf(stderr, "BulkIO server startup failed\
				with error %d\n", rc);
			return rc;
		}
		C2_ASSERT(bp->bp_sctx != NULL);
	}

	if (client) {
		rc = bulkio_client_start(bp, caddr, port, saddr, port);
		if (rc != 0) {
			fprintf(stderr, "BulkIO client startup failed\
				with error %d\n", rc);
			return rc;
		}
		C2_ASSERT(bp->bp_cctx != NULL);
		/*
		 * Send multiple IO fops, each with a distinct IO vector
		 * attached with it. IO coalescing occurs if multiple IO fops
		 * with same fid and same intent (read/write) are present
		 * in session unbound items list at the same time.
		 * This calls waits until replies for all IO fops are received.
		 */
		bulkio_test(bp, IO_FIDS_NR, IO_FOPS_NR, IO_SEGS_NR);

		bulkio_client_stop(bp->bp_cctx);
	}

	/*
	 * If only server is running, client is running from somewhere else
	 * and server should wait until client has sent all fops and
	 * all fops are processed.
	 * Also, if only server is running, we need a hack to redirect
	 * fops to UT fom code instead of standard io foms since old
	 * io foms are based on sunrpc.
	 */
	if (server && !client) {
		//io_fop_rwv_ops.fto_fom_init = io_fop_dummy_fom_init;
		fprintf(stderr, "Press Ctrl-C to stop bulk IO server.\n");
		while (!stop_server) {
			sleep(1);
			print_stats(bp);
		}
	}

	if (server)
		bulkio_server_stop(bp->bp_sctx);

	bulkio_params_fini(bp);
	c2_free(bp);

	c2_fini();

	return rc;
}
