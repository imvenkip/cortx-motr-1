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
 * Original creation date: 08/24/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>     /* feof, fscanf, ... */
#include <err.h>

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "addb/addb.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"
#include "rpc/rpclib.h"
#include "lib/processor.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "stob/io_fop.h"
#include "colibri/init.h"

#include "stob/io_fop_u.h"

/**
   @addtogroup stob
   @{
 */

#define SERVER_ENDPOINT_ADDR	"127.0.0.1:12346:1"
#define CLIENT_ENDPOINT_ADDR	"127.0.0.1:12347:1"
#define CLIENT_DB_NAME		"stob_ut_client"

enum {
	CLIENT_COB_DOM_ID	= 18,
	SESSION_SLOTS		= 1,
	MAX_RPCS_IN_FLIGHT	= 1,
	CONNECT_TIMEOUT		= 5,
};

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;

int got_quit = 0;


static void create_send(struct c2_rpc_session *session, const struct stob_io_fop_fid *fid)
{
	int rc;
	struct c2_fop                   *fop;
	struct c2_fop                   *rep;
	struct c2_stob_io_create	*fop_data;
	struct c2_stob_io_create_rep	*rep_data;

	fop = c2_fop_alloc(&c2_stob_io_create_fopt, NULL);
	fop_data = c2_fop_data(fop);
	fop_data->fic_object = *fid;
	rc = c2_rpc_client_call(fop, session, CONNECT_TIMEOUT);
	C2_ASSERT(rc == 0);
	C2_ASSERT(fop->f_item.ri_error == 0);
	C2_ASSERT(fop->f_item.ri_reply != 0);
	rep = container_of(fop->f_item.ri_reply, struct c2_fop, f_item);
	rep_data = c2_fop_data(rep);
	printf("GOT: %d %d\n", rc, rep_data->ficr_rc);
}

static void read_send(struct c2_rpc_session *session, const struct stob_io_fop_fid *fid)
{
	int rc;
	struct c2_fop                   *fop;
	struct c2_fop                   *rep;
	struct c2_stob_io_read		*fop_data;
	struct c2_stob_io_read_rep	*rep_data;

	fop = c2_fop_alloc(&c2_stob_io_read_fopt, NULL);
	fop_data = c2_fop_data(fop);
	fop_data->fir_object = *fid;

	rc = c2_rpc_client_call(fop, session, CONNECT_TIMEOUT);
	C2_ASSERT(rc == 0);
	C2_ASSERT(fop->f_item.ri_error == 0);
	C2_ASSERT(fop->f_item.ri_reply != 0);

	rep = container_of(fop->f_item.ri_reply, struct c2_fop, f_item);
	rep_data = c2_fop_data(rep);
	printf("GOT: %d %d %u %c\n", rc, rep_data->firr_rc,
			rep_data->firr_count, rep_data->firr_value);
}

static void write_send(struct c2_rpc_session *session, const struct stob_io_fop_fid *fid)
{
	int rc;
	struct c2_fop                   *fop;
	struct c2_fop                   *rep;
	struct c2_stob_io_write		*fop_data;
	struct c2_stob_io_write_rep	*rep_data;

	fop = c2_fop_alloc(&c2_stob_io_write_fopt, NULL);
	fop_data = c2_fop_data(fop);
	fop_data->fiw_object = *fid;
	fop_data->fiw_value = 'x';

	rc = c2_rpc_client_call(fop, session, CONNECT_TIMEOUT);
	C2_ASSERT(rc == 0);
	C2_ASSERT(fop->f_item.ri_error == 0);
	C2_ASSERT(fop->f_item.ri_reply != 0);

	rep = container_of(fop->f_item.ri_reply, struct c2_fop, f_item);
	rep_data = c2_fop_data(rep);
	printf("GOT: %d %d %u\n", rc, rep_data->fiwr_rc,
			rep_data->fiwr_count);
}

/**
   Simple client.

   Synopsis:

     client [ip_addr:port:id]

   After connecting to the server, the client reads commands from the standard
   input. The following commands are supported:

       c <D1> <D2>

           Create an object with the fid (D1:D2)

       w <D1> <D2>

           Write into an object with the fid (D1:D2).

       r <D1> <D2>

           Read from an object with the fid (D1:D2).
 */
int main(int argc, char **argv)
{
	int rc;
	struct c2_net_xprt    *xprt = &c2_net_bulk_sunrpc_xprt;
	struct c2_net_domain  net_dom = { };

	struct c2_rpc_ctx client_rctx = {
		.rx_net_dom            = &net_dom,
		.rx_reqh               = NULL,
		.rx_local_addr         = CLIENT_ENDPOINT_ADDR,
		.rx_remote_addr        = SERVER_ENDPOINT_ADDR,
		.rx_db_name            = CLIENT_DB_NAME,
		.rx_cob_dom_id         = CLIENT_COB_DOM_ID,
		.rx_nr_slots           = SESSION_SLOTS,
		.rx_timeout_s          = CONNECT_TIMEOUT,
		.rx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
	};

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	if (argc > 1)
		client_rctx.rx_remote_addr = argv[1];

	rc = c2_init();
	C2_ASSERT(rc == 0);

	rc = c2_processors_init();
	C2_ASSERT(rc == 0);

	rc = c2_stob_io_fop_init();
	C2_ASSERT(rc == 0);

	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&net_dom, xprt);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_client_init(&client_rctx);
	C2_ASSERT(rc == 0);

	printf("cmd> ");

	while (!feof(stdin)) {
		struct stob_io_fop_fid fid;
		char                   cmd;
		char                   *line = NULL;
		size_t                 n = 0;

		rc = getline(&line, &n, stdin);
		if (rc == -1) {
			if (feof(stdin))
				return EXIT_SUCCESS;
			else
				err(EXIT_FAILURE, "failed to read line from STDIN");
		}

		n = sscanf(line, "%c %lu %lu\n", &cmd, (unsigned long *)&fid.f_seq,
			  (unsigned long *)&fid.f_oid);
		if (n != 3)
			err(1, "wrong conversion: %zd", n);

		free(line);

		switch (cmd) {
		case 'c':
			create_send(&client_rctx.rx_session, &fid);
			break;
		case 'r':
			read_send(&client_rctx.rx_session, &fid);
			break;
		case 'w':
			write_send(&client_rctx.rx_session, &fid);
			break;
		case 'q':
			got_quit = 1;
			break;
		default:
			err(1, "Unknown command '%c'", cmd);
		}

		if (got_quit)
			break;

		printf("cmd> ");
	}

	rc = c2_rpc_client_fini(&client_rctx);
	C2_ASSERT(rc == 0);

	c2_net_domain_fini(&net_dom);
	c2_net_xprt_fini(xprt);
	c2_stob_io_fop_fini();
	c2_fini();

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
