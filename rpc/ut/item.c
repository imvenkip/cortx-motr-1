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
 * Original author: Amit Jambure<amit_jambure@xyratex.com>
 * Original creation date: 10/19/2012
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "lib/trace.h"
#include "lib/finject.h"
#include "lib/time.h"
#include "addb/addb.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#include "rpc/session.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fop_ff.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"

#include "ut/rpc.h"
#include "ut/cs_service.h"
#include "ut/cs_fop_foms.h"
#include "ut/cs_test_fops_ff.h"

#include "rpc/ut/clnt_srv_ctx.c"   /* sctx, cctx */

static struct c2_fop *fop_alloc(void)
{
	struct cs_ds2_req_fop *cs_ds2_fop;
	struct c2_fop         *fop;

	fop = c2_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
	C2_UT_ASSERT(fop != NULL);

	cs_ds2_fop = c2_fop_data(fop);
	cs_ds2_fop->csr_value = 0xaaf5;

	return fop;
}


static int ts_item_init(void)
{
	int rc;

	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&client_net_dom, xprt);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_server_start(&sctx);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_client_init(&cctx);
	C2_ASSERT(rc == 0);

	return rc;
}

static int ts_item_fini(void)
{
	int rc;

	rc = c2_rpc_client_fini(&cctx);
	C2_ASSERT(rc == 0);
	c2_rpc_server_stop(&sctx);
	c2_net_domain_fini(&client_net_dom);
	c2_net_xprt_fini(xprt);
	return rc;
}

/* static */const char *item_state_name(const struct c2_rpc_item *item)
{
	return item->ri_sm.sm_conf->scf_state[item->ri_sm.sm_state].sd_name;
}

static bool chk_state(const struct c2_rpc_item *item,
		      enum c2_rpc_item_state    state)
{
	return item->ri_sm.sm_state == state;
}

static void test_transitions(void)
{
	struct c2_rpc_machine *machine;
	struct c2_rpc_stats    saved;
	struct c2_rpc_stats    stats;
	struct c2_rpc_item    *item;
	struct c2_fop         *fop;
	int                    rc;

#define IS_INCR_BY_1(p) (saved.rs_ ## p + 1 == stats.rs_ ## p)
	machine = cctx.rcx_session.s_conn->c_rpc_machine;
	C2_LOG(C2_DEBUG, "TEST:1:START");
	c2_rpc_machine_get_stats(machine, &saved, false /* clear stats? */);
	fop = fop_alloc();
	item = &fop->f_item;
	rc = c2_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				CONNECT_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(item->ri_error == 0);
	C2_UT_ASSERT(item->ri_reply != NULL);
	C2_UT_ASSERT(chk_state(item, C2_RPC_ITEM_REPLIED) &&
		     chk_state(item->ri_reply, C2_RPC_ITEM_ACCEPTED));
	c2_rpc_machine_get_stats(machine, &stats, true);
	C2_UT_ASSERT(IS_INCR_BY_1(nr_sent_items) &&
		     IS_INCR_BY_1(nr_rcvd_items));
	C2_LOG(C2_DEBUG, "TEST:1:END");
	/* A reference to item and item->ri_reply is maintained in slot's
	   item list.
	   Objects pointed by item and item->ri_reply will be freed during
	   c2_rpc_session_fini().
	   Hence we can reuse 'fop*' and 'item*' variables.
	 */

	C2_LOG(C2_DEBUG, "TEST:2:START");
	fop = fop_alloc();
	item = &fop->f_item;
	c2_rpc_machine_get_stats(machine, &saved, false);
	c2_fi_enable_once("cs_req_fop_fom_tick", "inject_delay");
	rc = c2_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				1 /* timeout in seconds */);
	C2_UT_ASSERT(rc == -ETIMEDOUT);
	C2_UT_ASSERT(item->ri_error == -ETIMEDOUT);
	C2_UT_ASSERT(item->ri_reply == NULL);
	C2_UT_ASSERT(chk_state(item, C2_RPC_ITEM_FAILED));
	c2_nanosleep(c2_time(2, 0), NULL);
	c2_rpc_machine_get_stats(machine, &stats, true);
	C2_UT_ASSERT(IS_INCR_BY_1(nr_dropped_items) &&
		     IS_INCR_BY_1(nr_timedout_items));
	C2_LOG(C2_DEBUG, "TEST:2:END");
}

const struct c2_test_suite item_ut= {
	.ts_name = "item-ut",
	.ts_init = ts_item_init,
	.ts_fini = ts_item_fini,
	.ts_tests = {
		{ "item-transitions", test_transitions},
		{ NULL, NULL },
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
