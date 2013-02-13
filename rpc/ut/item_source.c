/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 11-Feb-2013
 */


/**
 * @addtogroup rpc
 *
 * @{
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "lib/misc.h"              /* M0_BITS */
#include "lib/time.h"              /* m0_nanosleep */
#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"
#include "rpc/ut/clnt_srv_ctx.c"
#include "rpc/ut/rpc_test_fops.h"  /* m0_rpc_arrow_fopt */
#include "ut/cs_fop_foms.h"        /* cs_ds2_req_fop_fopt */
#include "ut/cs_fop_foms_xc.h"     /* cs_ds2_req_fop */

#include <stdio.h>

static struct m0_rpc_conn *conn;
static bool conn_terminating_cb_called;

static int item_source_test_suite_init(void)
{
	m0_rpc_test_fops_init();
	start_rpc_client_and_server();
	conn = &cctx.rcx_connection;
	return 0;
}

static int item_source_test_suite_fini(void)
{
	m0_rpc_test_fops_fini();
	return 0;
}

static bool has_item_flag = false;
static struct m0_rpc_item *item = NULL;

static bool has_item_called;
static bool get_item_called;

static bool has_item(const struct m0_rpc_item_source *ris)
{
	M0_UT_ASSERT(m0_rpc_machine_is_locked(ris->ris_conn->c_rpc_machine));
	if (has_item_called)
		return false;

	has_item_called = true;
	return true;
}

static struct m0_rpc_item *get_item(struct m0_rpc_item_source *ris,
				    size_t max_payload_size)
{
	struct m0_fop *fop;

	M0_UT_ASSERT(m0_rpc_machine_is_locked(ris->ris_conn->c_rpc_machine));
	get_item_called = true;

	fop  = m0_fop_alloc(&m0_rpc_arrow_fopt, NULL);
	M0_UT_ASSERT(fop != NULL);
	item = &fop->f_item;
	/* without this "get", the item will be freed as soon as it is
	   sent/failed. The reference is required to protect item until
	   item_source_test() performs its checks on the item.
	 */
	m0_rpc_item_get(item);

	M0_UT_ASSERT(m0_rpc_item_is_oneway(item) &&
		     m0_rpc_item_payload_size(item) <= max_payload_size);

	return item;
}

static void conn_terminating(struct m0_rpc_item_source *ris)
{
	M0_UT_ASSERT(!m0_rpc_item_source_is_registered(ris));

	conn_terminating_cb_called = true;
	m0_rpc_item_source_fini(ris);
	m0_free(ris);

	return;
}

static const struct m0_rpc_item_source_ops ris_ops = {
	.riso_has_item         = has_item,
	.riso_get_item         = get_item,
	.riso_conn_terminating = conn_terminating,
};

static void item_source_basic_test(void)
{
	struct m0_rpc_item_source ris;
	int rc;

	rc = m0_rpc_item_source_init(&ris, "test-item-source", &ris_ops);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(ris.ris_ops == &ris_ops);
	m0_rpc_item_source_register(conn, &ris);
	m0_rpc_item_source_deregister(&ris);
	m0_rpc_item_source_fini(&ris);
}

static void item_source_test(void)
{
	enum {MILLISEC = 1000 * 1000 };
	struct m0_rpc_item_source  *ris;
	bool                        ok;
	int                         trigger;
	int                         rc;

	/*
	   Test:
	   - Confirm that formation correctly pulls items and sends them.
	   - Also verify that periodic item-source drain works.
	 */
	M0_ALLOC_PTR(ris);
	M0_UT_ASSERT(ris != NULL);
	rc = m0_rpc_item_source_init(ris, "test-item-source", &ris_ops);
	m0_rpc_item_source_register(conn, ris);

	for (trigger = 0; trigger < 2; trigger++) {
		has_item_flag = true;
		m0_fi_enable("frm_is_ready", "ready");
		has_item_called = get_item_called = false;
		switch (trigger) {
		case 0:
			m0_rpc_machine_lock(conn->c_rpc_machine);
			m0_rpc_frm_run_formation(&conn->c_rpcchan->rc_frm);
			m0_rpc_machine_unlock(conn->c_rpc_machine);
			break;
		case 1:
			/* Wake-up rpc-worker thread */
			m0_clink_signal(
				&conn->c_rpc_machine->rm_sm_grp.s_clink);
			/* Give rpc-worker thread a chance to run. */
			m0_nanosleep(m0_time(0, 100 * MILLISEC), NULL);
			break;
		default:
			M0_IMPOSSIBLE("only two triggers");
		}
		M0_UT_ASSERT(has_item_called && get_item_called);
		rc = m0_rpc_item_timedwait(item, M0_BITS(M0_RPC_ITEM_SENT,
							 M0_RPC_ITEM_FAILED),
					   m0_time_from_now(2, 0));
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(item->ri_sm.sm_state == M0_RPC_ITEM_SENT);

		/* wait until the fop is executed on the receiver */
		ok = m0_semaphore_timeddown(&arrow_hit, m0_time_from_now(5, 0));
		M0_UT_ASSERT(ok);

		/* wait until received fop is freed on the receiver */
		ok = m0_semaphore_timeddown(&arrow_destroyed,
					    m0_time_from_now(5, 0));
		M0_UT_ASSERT(ok);

		m0_fi_disable("frm_is_ready", "ready");
		m0_rpc_item_put(item);
	}
	m0_rpc_item_source_deregister(ris);
	m0_rpc_item_source_fini(ris);
	m0_free(ris);
}

static void conn_terminating_cb_test(void)
{
	struct m0_rpc_item_source *ris;
	int                        rc;

	M0_ALLOC_PTR(ris);
	M0_UT_ASSERT(ris != NULL);
	rc = m0_rpc_item_source_init(ris, "test-item-source", &ris_ops);
	M0_UT_ASSERT(rc == 0);
	m0_rpc_item_source_register(conn, ris);

	M0_UT_ASSERT(!conn_terminating_cb_called);
	stop_rpc_client_and_server();
	/* riso_conn_terminating() callback will be called on item-sources,
	   which were still registered when rpc-conn was being terminated
	 */
	M0_UT_ASSERT(conn_terminating_cb_called);
}

const struct m0_test_suite item_source_ut = {
	.ts_name = "rpc-item-source-ut",
	.ts_init = item_source_test_suite_init,
	.ts_fini = item_source_test_suite_fini,
	.ts_tests = {
		{ "basic",                    item_source_basic_test   },
		{ "item_pull",                item_source_test         },
		{ "conn_terminating_cb_test", conn_terminating_cb_test },
		{ NULL,                       NULL                     },
	}
};

/** @} end of rpc group */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
