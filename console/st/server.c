/* -*- C -*- */
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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#include "lib/errno.h"            /* ETIMEDOUT */
#include "net/bulk_sunrpc.h"      /* bulk transport */
#include "colibri/init.h"         /* c2_init */
#include "lib/getopts.h"	  /* C2_GETOPTS */
#include "lib/misc.h"		  /* C2_SET0 */
#include "lib/processor.h"	  /* c2_processors_init/fini */

#include "console/console.h"
#include "console/console_fop.h"

/**
   @addtogroup console main
   @{
 */

struct c2_console cons_server = {
	.cons_lhost	      = "localhost",
	.cons_lport	      = SERVER_PORT,
	.cons_rhost	      = "localhost",
	.cons_rport	      = CLIENT_PORT,
	.cons_db_name	      = "cons_server_db",
	.cons_cob_dom_id      = { .id = 15 },
	.cons_nr_slots	      = NR_SLOTS,
	.cons_rid	      = RID,
	.cons_xprt	      = &c2_net_bulk_sunrpc_xprt,
	.cons_items_in_flight = MAX_RPCS_IN_FLIGHT
};

static int signaled = 0;

static void sig_handler(int num)
{
	signaled = 1;
}
/**
 * @brief Console is stand alone program to send specified FOP
 *	  to IO service.
 *
 *	  Usage:
 *	  where valid options are
 *
 *	 -l       : show list of fops
 *	 -t    arg: fop type
 *	 -s string: server host name
 *	 -p    arg: server port
 *	 -i       : yaml input
 *	 -y string: yaml file path
 *	 -v       : verbose
 *
 *
 * @return 0 success, -errno failure.
 */
int main(int argc, char **argv)
{
	uint32_t    port = 0;
	int	    result;
	const char *client = NULL;

	verbose = false;
	result = c2_init();
	if (result != 0) {
		printf("c2_init failed\n");
		goto end0;
	}

	/*
	 * Gets the info to connect to the service and type of fop to be send.
	 */
	result = C2_GETOPTS("server", argc, argv,
			C2_STRINGARG('s', "remote host name",
			LAMBDA(void, (const char *name) { client = name; })),
			C2_FORMATARG('p', "remote host port", "%i", &port),
			C2_FLAGARG('v', "verbose", &verbose));

	if (result != 0) {
		printf("c2_getopts failed\n");
		goto end1;
	}

	if (client != NULL)
		cons_server.cons_rhost = client;

	if (port != 0)
		cons_server.cons_rport = port;

	result = c2_console_fop_init();
	if (result != 0) {
		printf("c2_console_fop_init failed\n");
		goto end1;
	}

	result = c2_processors_init();
	if (result != 0) {
		printf("c2_processors_init failed\n");
		goto end2;
	}

	result = c2_cons_rpc_server_init(&cons_server);
	if (result != 0) {
		printf("c2_console_init failed\n");
		goto fini;
	}

        printf("Server Address = %s\n", cons_server.cons_laddr);
        printf("Console Address = %s\n", cons_server.cons_raddr);

	printf("Press CTRL+C to quit.\n");
	signal(SIGINT, sig_handler);
	while (!signaled);
	printf("\nExiting Server.\n");
fini:
	c2_cons_rpc_server_fini(&cons_server);
	c2_processors_fini();
end2:
	c2_console_fop_fini();
end1:
	c2_fini();
end0:
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
