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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 */

#include <signal.h>
#include <unistd.h>               /* sleep */

#include "net/lnet/lnet.h"
#include "colibri/init.h"         /* c2_init */
#include "lib/getopts.h"	  /* C2_GETOPTS */

#include "rpc/rpclib.h"           /* c2_rpc_server_start */
#include "ut/rpc.h"               /* C2_RPC_SERVER_CTX_DEFINE */

#include "console/console.h"
#include "console/console_fop.h"

/**
   @addtogroup console
   @{
 */

#define SERVER_ENDPOINT_ADDR	"0@lo:12345:34:1"
#define SERVER_ENDPOINT		"lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"cons_server.db"
#define SERVER_STOB_FILE_NAME	"cons_server.stob"
#define SERVER_LOG_FILE_NAME	"cons_server.log"
#define CONSOLE_STR_LEN         16

static int signaled = 0;

static void sig_handler(int num)
{
	signaled = 1;
}

/** @brief Test server for c2console */
int main(int argc, char **argv)
{
	int                 result;
	uint32_t            tm_recv_queue_len = C2_NET_TM_RECV_QUEUE_DEF_LEN;
	uint32_t            max_rpc_msg_size  = C2_RPC_DEF_MAX_RPC_MSG_SIZE;
	char                tm_len[CONSOLE_STR_LEN];
	char                rpc_size[CONSOLE_STR_LEN];
	struct c2_net_xprt *xprt              = &c2_net_lnet_xprt;

	char *default_server_argv[] = {
		argv[0], "-r", "-p", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
		"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
		"-s", "ds1", "-s", "ds2", "-q", tm_len, "-m", rpc_size
	};

	C2_RPC_SERVER_CTX_DEFINE_SIMPLE(sctx, xprt, default_server_argv,
					SERVER_LOG_FILE_NAME);

	verbose = false;

	result = C2_GETOPTS("server", argc, argv,
			    C2_FLAGARG('v', "verbose", &verbose),
			    C2_FORMATARG('q', "minimum TM receive queue length",
					 "%i", &tm_recv_queue_len),
			    C2_FORMATARG('m', "max rpc msg size", "%i",
					 &max_rpc_msg_size),);

	if (result != 0) {
		printf("c2_getopts failed\n");
		return result;
	}

	sprintf(tm_len, "%d" , tm_recv_queue_len);
	sprintf(rpc_size, "%d" , max_rpc_msg_size);

	result = c2_init();
	if (result != 0) {
		printf("c2_init failed\n");
		return result;
	}

	result = c2_console_fop_init();
	if (result != 0) {
		printf("c2_console_fop_init failed\n");
		goto c2_fini;
	}

	result = c2_rpc_server_start(&sctx);
	if (result != 0) {
		printf("failed to start rpc server\n");
		goto fop_fini;
	}

	printf("Press CTRL+C to quit.\n");

	signal(SIGINT, sig_handler);
	while (!signaled)
		sleep(1);
	printf("\nExiting Server.\n");

	c2_rpc_server_stop(&sctx);
fop_fini:
	c2_console_fop_fini();
c2_fini:
	c2_fini();
	return result;
}

/** @} end of console group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
