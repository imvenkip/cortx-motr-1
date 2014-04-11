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
#include <unistd.h>             /* sleep */

#include "net/lnet/lnet.h"
#include "mero/init.h"          /* m0_init */
#include "lib/getopts.h"        /* M0_GETOPTS */

#include "rpc/rpclib.h"         /* m0_rpc_server_start */
#include "ut/cs_service.h"      /* m0_cs_default_stypes */
#include "ut/ut.h"              /* m0_ut_init */

#include "console/console.h"
#include "console/console_fop.h"

/**
   @addtogroup console
   @{
 */

#define SERVER_ENDPOINT_ADDR  "0@lo:12345:34:1"
#define SERVER_ENDPOINT       "lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME   "cons_server.db"
#define SERVER_STOB_FILE_NAME "cons_server.stob"
#define SERVER_LOG_FILE_NAME  "cons_server.log"

static int signaled = 0;

static void sig_handler(int num)
{
	signaled = 1;
}

/** @brief Test server for m0console */
int main(int argc, char **argv)
{
	enum { CONSOLE_STR_LEN = 16 };
	char     tm_len[CONSOLE_STR_LEN];
	char     rpc_size[CONSOLE_STR_LEN];
	int      result;
	uint32_t tm_recv_queue_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	uint32_t max_rpc_msg_size  = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
	char    *default_server_argv[] = {
		argv[0], "-T", "AD", "-D", SERVER_DB_FILE_NAME,
		"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
		"-s", "ds1", "-s", "ds2", "-s", "ioservice", "-q", tm_len,
		"-m", rpc_size, "-A", "linuxstob:as_addb_stob"
	};
	struct m0_net_xprt      *xprt = &m0_net_lnet_xprt;
	struct m0_rpc_server_ctx sctx = {
		.rsx_xprts            = &xprt,
		.rsx_xprts_nr         = 1,
		.rsx_argv             = default_server_argv,
		.rsx_argc             = ARRAY_SIZE(default_server_argv),
		.rsx_service_types    = m0_cs_default_stypes,
		.rsx_service_types_nr = m0_cs_default_stypes_nr,
		.rsx_log_file_name    = SERVER_LOG_FILE_NAME
	};

	m0_console_verbose = false;

	result = M0_GETOPTS("server", argc, argv,
			    M0_FLAGARG('v', "verbose", &m0_console_verbose),
			    M0_FORMATARG('q', "minimum TM receive queue length",
					 "%i", &tm_recv_queue_len),
			    M0_FORMATARG('m', "max rpc msg size", "%i",
					 &max_rpc_msg_size),);

	if (result != 0) {
		printf("m0_getopts failed\n");
		return result;
	}

	sprintf(tm_len, "%d" , tm_recv_queue_len);
	sprintf(rpc_size, "%d" , max_rpc_msg_size);

	result = m0_init(NULL);
	if (result != 0) {
		printf("m0_init failed\n");
		return result;
	}
	result = m0_ut_init();
	if (result != 0) {
		printf("m0_ut_init failed\n");
		return result;
	}

	result = m0_console_fop_init();
	if (result != 0) {
		printf("m0_console_fop_init failed\n");
		goto m0_fini;
	}

	result = m0_rpc_server_start(&sctx);
	if (result != 0) {
		printf("failed to start rpc server\n");
		goto fop_fini;
	}

	printf("Press CTRL+C to quit.\n");

	signal(SIGINT, sig_handler);
	while (!signaled)
		sleep(1);
	printf("\nExiting Server.\n");

	m0_rpc_server_stop(&sctx);
fop_fini:
	m0_console_fop_fini();
m0_fini:
	m0_ut_fini();
	m0_fini();
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
