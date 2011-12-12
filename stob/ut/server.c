/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/21/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>     /* printf, perror */
#include <unistd.h>    /* sleep */
#include <signal.h>    /* sigaction */

#include "colibri/init.h" /* c2_init */
#include "rpc/rpclib.h"   /* c2_rpc_server_start */
#include "ut/rpc.h"       /* C2_RPC_SERVER_CTX_DECLARE */

#include "stob/io_fop.h"  /* c2_stob_io_fop_init */
#include "stob/io_fop_u.h"


/**
   @addtogroup stob
   @{
 */

#define SERVER_ENDPOINT_ADDR	"127.0.0.1:12346:1"
#define SERVER_ENDPOINT		"bulk-sunrpc:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"stob_ut_server.db"
#define SERVER_STOB_FILE_NAME	"stob_ut_server.stob"
#define SERVER_LOG_FILE_NAME	"stob_ut_server.log"

static bool stop = false;

static void sigint_handler(int signum)
{
	stop = true;
}

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;

/**
   Simple server for unit-test purposes.

   Synopsis:

       server [options]

       Options can be any valid colibri_setup option. Please, see cs_help() in
       colibri/colibri_setup.c for reference.
 */
int main(int argc, char **argv)
{
	int rc;
	struct c2_net_xprt *xprt = &c2_net_bulk_sunrpc_xprt;

	struct sigaction sigint_action = {
		.sa_handler = sigint_handler,
	};

	char *default_server_argv[] = {
		argv[0], "-r", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
		"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
		"-s", "ds1", "-s", "ds2"
	};

	C2_RPC_SERVER_CTX_DECLARE(sctx, xprt, default_server_argv,
				  SERVER_LOG_FILE_NAME);

	/*
	 * If there are some CLI options, use them as input parameters for
	 * c2_cs_setup_env(), otherwise use default_server_argv options.
	 */
	if (argc > 1) {
		sctx.rsx_argc = argc;
		sctx.rsx_argv = argv;
	}

	rc = sigaction(SIGINT, &sigint_action, NULL);
	if (rc != 0) {
		perror("Failed to set signal handler for SIGINT\n");
		return rc;
	}

	printf("Starting server...");

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = c2_stob_io_fop_init();
	C2_ASSERT(rc == 0);

	rc = c2_rpc_server_start(&sctx);
	C2_ASSERT(rc == 0);

	printf(" done\n");
	printf("Press ^C to quit\n");

	while (!stop) {
		sleep(1);
        }

	printf("\nStopping server...");

	c2_rpc_server_stop(&sctx);
	c2_stob_io_fop_fini();
	c2_fini();

	printf(" done\n");

	return 0;
}

/** @} end group stob */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
