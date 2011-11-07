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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 09/28/2011
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "addb/addb.h"
#include "rpc/session.h"
#include "rpc/it/ping_fop.h"
#include "fop/fop.h"
#include "lib/processor.h"

#ifdef __KERNEL__
#include "rpc/it/ping_fop_k.h"
#else
#include "rpc/it/ping_fop_u.h"
#endif

#include "rpc/rpclib.h"


#define SERVER_ENDPOINT_ADDR	"127.0.0.1:12345:1"
#define CLIENT_ENDPOINT_ADDR	"127.0.0.1:12345:2"
#define SERVER_DB_NAME		"rpclib_ut_db_server"
#define CLIENT_DB_NAME		"rpclib_ut_db_client"

enum {
	CLIENT_COB_DOM_ID	= 16,
	SERVER_COB_DOM_ID	= 17,
	SESSION_SLOTS		= 1,
	MAX_RPCS_IN_FLIGHT	= 1,
	CONNECT_TIMEOUT		= 5,
	NR_PING_BYTES		= 8,
};

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;


static int send_fop(struct c2_rpc_session *session)
{
	int                rc;
	int                i;
	struct c2_fop      *fop;
	struct c2_fop_ping *ping_fop;

	fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	C2_UT_ASSERT(fop != NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	ping_fop = c2_fop_data(fop);
	ping_fop->fp_arr.f_count = NR_PING_BYTES / sizeof(ping_fop->fp_arr.f_data);

	C2_ALLOC_ARR(ping_fop->fp_arr.f_data, ping_fop->fp_arr.f_count);
	C2_UT_ASSERT(ping_fop->fp_arr.f_data != NULL);
	if (ping_fop->fp_arr.f_data == NULL) {
		rc = -ENOMEM;
		goto free_fop;
	}

	for (i = 0; i < ping_fop->fp_arr.f_count; i++) {
		ping_fop->fp_arr.f_data[i] = i+100;
	}

	rc = c2_rpc_client_call(fop, session, CONNECT_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fop->f_item.ri_error == 0);
	C2_UT_ASSERT(fop->f_item.ri_reply != 0);

	c2_free(ping_fop->fp_arr.f_data);
free_fop:
	/* FIXME: freeing fop here will lead to endless loop in
	 * nr_active_items_count(), which is called from
	 * c2_rpc_session_terminate() */
out:
	return rc;
}

static void test_rpclib(void)
{
	int rc;
	struct c2_net_xprt    *xprt = &c2_net_bulk_sunrpc_xprt;
	struct c2_net_domain  net_dom = { };

	struct c2_rpc_ctx server_rctx = {
		.rx_net_dom            = &net_dom,
		.rx_reqh               = NULL,
		.rx_local_addr         = SERVER_ENDPOINT_ADDR,
		.rx_remote_addr        = CLIENT_ENDPOINT_ADDR,
		.rx_db_name            = SERVER_DB_NAME,
		.rx_cob_dom_id         = SERVER_COB_DOM_ID,
		.rx_nr_slots           = SESSION_SLOTS,
		.rx_timeout_s          = CONNECT_TIMEOUT,
		.rx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
	};

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

	rc = c2_net_xprt_init(xprt);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto out;

	rc = c2_net_domain_init(&net_dom, xprt);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto xprt_fini;

	rc = c2_rpc_server_init(&server_rctx);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto net_dom_fini;

	rc = c2_rpc_client_init(&client_rctx);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto server_fini;

	rc = send_fop(&client_rctx.rx_session);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_client_fini(&client_rctx);
	C2_UT_ASSERT(rc == 0);

server_fini:
	c2_rpc_server_fini(&server_rctx);
net_dom_fini:
	c2_net_domain_fini(&net_dom);
xprt_fini:
	c2_net_xprt_fini(xprt);
out:
	return;
}

static int test_rpclib_init(void)
{
	int rc;

	/* set ADDB leve to AEL_WARN to see ADDB messages on STDOUT */
	/*c2_addb_choose_default_level(AEL_WARN);*/

	rc = c2_processors_init();
	C2_ASSERT(rc == 0);

	rc = c2_ping_fop_init();
	C2_ASSERT(rc == 0);

	return rc;
}

static int test_rpclib_fini(void)
{
	c2_ping_fop_fini();
	c2_processors_fini();

	return 0;
}

const struct c2_test_suite rpclib_ut = {
	.ts_name = "rpclib-ut",
	.ts_init = test_rpclib_init,
	.ts_fini = test_rpclib_fini,
	.ts_tests = {
		{ "rpclib", test_rpclib },
		{ NULL, NULL }
	}
};


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
