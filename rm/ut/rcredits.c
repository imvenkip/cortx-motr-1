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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 08/21/2012
 */
#include "lib/types.h"            /* uint64_t */
#include "lib/chan.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/time.h"
#include "lib/ut.h"
#include "db/db.h"
#include "cob/cob.h"
#include "net/lnet/lnet.h"
#include "net/buffer_pool.h"
#include "mdstore/mdstore.h"
#include "reqh/reqh.h"
#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"
#include "rpc/rpc_machine.h"
#include "fop/fom_generic.h"
#include "fop/fom_generic.h"

#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/rm_fops.h"
#include "rm/ut/rmut.h"

static const char *db_name[] = {"ut-rm-cob_1",
				"ut-rm-cob_2",
				"ut-rm-cob_3"
			       };

static const char *serv_addr[] = { "0@lo:12345:34:1",
				   "0@lo:12345:34:2",
				   "0@lo:12345:34:3"
				 };

/*
 * Hierarchy description:
 * SERVER_1 is downward debtor for SERVER_2.
 * SERVER_2 is upward creditor for SERVER_1 and downward debtor for SERVER_3.
 * SERVER_3 is upward creditor for SERVER_2.
 */

static const int cob_ids[] = { 20, 30, 40 };
/*
 * Buffer pool parameters.
 */
static uint32_t bp_buf_nr = 8;
static uint32_t bp_tm_nr = 2;

enum rm_server {
	SERVER_1 = 0,
	SERVER_2,
	SERVER_3,
	SERVER_NR,
	SERVER_INVALID,
};

enum rr_tests {
	TEST1 = 0,
	TEST2,
	TEST3,
	TEST4,
	TEST_NR,
};

/*
 * RM server context. It lives inside a thread in this test.
 */
struct rm_context {
	enum rm_server		  rc_id;
	const char		 *rc_ep_addr;
	struct m0_thread	  rc_thr;
	struct m0_chan		  rc_chan;
	struct m0_clink		  rc_clink;
	struct m0_mutex		  rc_mutex;
	struct m0_rpc_machine	  rc_rpc;
	struct m0_dbenv		  rc_dbenv;
	struct m0_fol		  rc_fol;
	struct m0_cob_domain_id	  rc_cob_id;
	struct m0_mdstore	  rc_mdstore;
	struct m0_cob_domain	  rc_cob_dom;
	struct m0_net_domain	  rc_net_dom;
	struct m0_net_buffer_pool rc_bufpool;
	struct m0_net_xprt	 *rc_xprt;
	struct m0_reqh		  rc_reqh;
	struct m0_net_end_point	 *rc_ep[SERVER_NR];
	struct m0_rpc_conn	  rc_conn[SERVER_NR];
	struct m0_rpc_session	  rc_sess[SERVER_NR];
	struct rm_ut_data	  rc_test_data;
	enum rm_server		  creditor_id;
	enum rm_server		  debtor_id;
};

static struct m0_chan rr_tests_chan;
static struct m0_clink tests_clink[TEST_NR];
static struct m0_mutex rr_tests_chan_mutex;

struct rm_context rm_ctx[SERVER_NR];

static void buf_empty(struct m0_net_buffer_pool *bp);
static void buf_low(struct m0_net_buffer_pool *bp);

const struct m0_net_buffer_pool_ops buf_ops = {
	.nbpo_below_threshold = buf_low,
	.nbpo_not_empty	      = buf_empty,
};

static void buf_empty(struct m0_net_buffer_pool *bp)
{
}

static void server1_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctx[SERVER_1].rc_chan);
}

static void server1_in_conflict(struct m0_rm_incoming *in)
{
}

const struct m0_rm_incoming_ops server1_incoming_ops = {
	.rio_complete = server1_in_complete,
	.rio_conflict = server1_in_conflict
};

static void server2_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctx[SERVER_2].rc_chan);
}

static void server2_in_conflict(struct m0_rm_incoming *in)
{
}

const struct m0_rm_incoming_ops server2_incoming_ops = {
	.rio_complete = server2_in_complete,
	.rio_conflict = server2_in_conflict
};

static void server3_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctx[SERVER_3].rc_chan);
}

static void server3_in_conflict(struct m0_rm_incoming *in)
{
}

const struct m0_rm_incoming_ops server3_incoming_ops = {
	.rio_complete = server3_in_complete,
	.rio_conflict = server3_in_conflict
};

static void buf_low(struct m0_net_buffer_pool *bp)
{
}

static void rm_ctx_init(struct rm_context *rmctx)
{
	int		rc;
	struct m0_db_tx tx;

	rmctx->rc_xprt = &m0_net_lnet_xprt;

	rc = m0_net_domain_init(&rmctx->rc_net_dom, rmctx->rc_xprt,
	                        &m0_addb_proc_ctx);
	M0_UT_ASSERT(rc == 0);

	rmctx->rc_bufpool.nbp_ops = &buf_ops;
	rc = m0_rpc_net_buffer_pool_setup(&rmctx->rc_net_dom,
					  &rmctx->rc_bufpool,
					  bp_buf_nr, bp_tm_nr);
	M0_UT_ASSERT(rc == 0);

	rc = m0_dbenv_init(&rmctx->rc_dbenv, db_name[rmctx->rc_id], 0);
	M0_UT_ASSERT(rc == 0);

	rc = m0_fol_init(&rmctx->rc_fol, &rmctx->rc_dbenv);
        M0_UT_ASSERT(rc == 0);

	rc = m0_cob_domain_init(&rmctx->rc_cob_dom, &rmctx->rc_dbenv,
				&rmctx->rc_cob_id);
	M0_UT_ASSERT(rc == 0);

	rc = m0_mdstore_init(&rmctx->rc_mdstore, &rmctx->rc_cob_id,
			     &rmctx->rc_dbenv, 0);
	M0_UT_ASSERT(rc == 0);

	rc = m0_db_tx_init(&tx, &rmctx->rc_dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rpc_root_session_cob_create(&rmctx->rc_mdstore.md_dom, &tx);
	M0_UT_ASSERT(rc == 0);
	m0_db_tx_commit(&tx);

	rc = M0_REQH_INIT(&rmctx->rc_reqh,
			.rhia_dtm       = (void*)1,
			.rhia_db        = &rmctx->rc_dbenv,
			.rhia_mdstore   = &rmctx->rc_mdstore,
			.rhia_fol       = &rmctx->rc_fol,
			.rhia_svc       = (void*)1,
			.rhia_addb_stob = NULL);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_machine_init(&rmctx->rc_rpc, &rmctx->rc_cob_dom,
				 &rmctx->rc_net_dom, rmctx->rc_ep_addr,
				 &rmctx->rc_reqh, &rmctx->rc_bufpool,
				 M0_BUFFER_ANY_COLOUR,
				 M0_RPC_DEF_MAX_RPC_MSG_SIZE,
				 M0_NET_TM_RECV_QUEUE_DEF_LEN);
	M0_UT_ASSERT(rc == 0);
	m0_mutex_init(&rmctx->rc_mutex);
	m0_chan_init(&rmctx->rc_chan, &rmctx->rc_mutex);
	m0_clink_init(&rmctx->rc_clink, NULL);
}

static void rm_ctx_fini(struct rm_context *rmctx)
{
	m0_clink_fini(&rmctx->rc_clink);
	m0_chan_fini_lock(&rmctx->rc_chan);
	m0_mutex_fini(&rmctx->rc_mutex);
	m0_rpc_machine_fini(&rmctx->rc_rpc);
	m0_reqh_fini(&rmctx->rc_reqh);
	m0_mdstore_fini(&rmctx->rc_mdstore);
	m0_cob_domain_fini(&rmctx->rc_cob_dom);
	m0_fol_fini(&rmctx->rc_fol);
	m0_dbenv_fini(&rmctx->rc_dbenv);
	m0_rpc_net_buffer_pool_cleanup(&rmctx->rc_bufpool);
	m0_net_domain_fini(&rmctx->rc_net_dom);
}

static void rm_connect(struct rm_context *src, const struct rm_context *dest)
{
	struct m0_net_end_point *ep;
	int		         rc;

	/*
	 * Create a local end point to communicate with remote server.
	 */
	rc = m0_net_end_point_create(&ep,
				     &src->rc_rpc.rm_tm,
				     dest->rc_ep_addr);
	M0_UT_ASSERT(rc == 0);
	src->rc_ep[dest->rc_id] = ep;

	rc = m0_rpc_conn_create(&src->rc_conn[dest->rc_id],
				ep, &src->rc_rpc, 15, m0_time_from_now(10, 0));
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_session_create(&src->rc_sess[dest->rc_id],
				   &src->rc_conn[dest->rc_id], 1,
				   m0_time_from_now(30, 0));
	M0_UT_ASSERT(rc == 0);
}

static void rm_disconnect(struct rm_context *src, const struct rm_context *dest)
{
	int rc;

	rc = m0_rpc_session_destroy(&src->rc_sess[dest->rc_id],
				    m0_time_from_now(30, 0));
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_conn_destroy(&src->rc_conn[dest->rc_id],
				 m0_time_from_now(30, 0));
	M0_UT_ASSERT(rc == 0);

	m0_net_end_point_put(src->rc_ep[dest->rc_id]);
	M0_UT_ASSERT(rc == 0);
}

static void server_start(enum rm_server srv_id)
{
	struct m0_rm_remote *creditor;
	struct m0_rm_owner  *owner = &rm_ctx[srv_id].rc_test_data.rd_owner;
	struct m0_rm_credit *credit = &rm_ctx[srv_id].rc_test_data.rd_credit;
	enum rm_server	     cred_id = rm_ctx[srv_id].creditor_id;
	enum rm_server	     debt_id = rm_ctx[srv_id].debtor_id;

	rm_utdata_init(&rm_ctx[srv_id].rc_test_data, OBJ_OWNER);
	/*
	 * If creditor id is valid, do creditor setup.
	 * If there is no creditor, this server is original owner.
	 * For original owner, raise capital.
	 */
	if (cred_id != SERVER_INVALID) {
		rm_connect(&rm_ctx[srv_id], &rm_ctx[cred_id]);
		M0_ALLOC_PTR(creditor);
		M0_UT_ASSERT(creditor != NULL);
		m0_rm_remote_init(creditor, owner->ro_resource);
		creditor->rem_session = &rm_ctx[srv_id].rc_sess[cred_id];
		owner->ro_creditor = creditor;
	} else
		rm_test_owner_capital_raise(owner, credit);

	if (debt_id != SERVER_INVALID)
		rm_connect(&rm_ctx[srv_id], &rm_ctx[debt_id]);

}

static void server_stop(enum rm_server srv_id)
{
	struct m0_rm_remote *creditor;
	struct m0_rm_owner  *owner = &rm_ctx[srv_id].rc_test_data.rd_owner;
	enum rm_server	     cred_id = rm_ctx[srv_id].creditor_id;
	enum rm_server	     debt_id = rm_ctx[srv_id].debtor_id;

	if (cred_id != SERVER_INVALID) {
		creditor = owner->ro_creditor;
		M0_UT_ASSERT(creditor != NULL);
		m0_rm_remote_fini(creditor);
		m0_free(creditor);
		owner->ro_creditor = NULL;
	}
	rm_utdata_fini(&rm_ctx[srv_id].rc_test_data, OBJ_OWNER);
	if (cred_id != SERVER_INVALID)
		rm_disconnect(&rm_ctx[srv_id], &rm_ctx[cred_id]);
	if (debt_id != SERVER_INVALID)
		rm_disconnect(&rm_ctx[srv_id], &rm_ctx[debt_id]);
}

static void creditor_cookie_setup(enum rm_server dsrv_id,
				  enum rm_server csrv_id)
{
	struct m0_rm_owner *creditor = &rm_ctx[csrv_id].rc_test_data.rd_owner;
	struct m0_rm_owner *owner = &rm_ctx[dsrv_id].rc_test_data.rd_owner;

	m0_cookie_init(&owner->ro_creditor->rem_cookie, &creditor->ro_id);

}

static void credit_setup(enum rm_server srv_id,
			enum m0_rm_incoming_flags flag,
			int value)
{
	struct m0_rm_incoming *in = &rm_ctx[srv_id].rc_test_data.rd_in;
	struct m0_rm_owner    *owner = &rm_ctx[srv_id].rc_test_data.rd_owner;

	m0_rm_incoming_init(in, owner, M0_RIT_LOCAL, RIP_NONE, flag);
	m0_rm_credit_init(&in->rin_want, owner);
	in->rin_want.cr_datum = value;
	switch (srv_id) {
	case SERVER_1:
		in->rin_ops = &server1_incoming_ops;
		break;
	case SERVER_2:
		in->rin_ops = &server2_incoming_ops;
		break;
	case SERVER_3:
		in->rin_ops = &server3_incoming_ops;
		break;
	default:
		break;
	}
}

static void loan_session_set(enum rm_server csrv_id,
			     enum rm_server dsrv_id)
{
	struct m0_rm_owner *owner = &rm_ctx[csrv_id].rc_test_data.rd_owner;
	struct m0_rm_loan  *loan;
	struct m0_rm_credit *credit;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&owner->ro_sublet));
	m0_tl_for(m0_rm_ur, &owner->ro_sublet, credit) {
		loan = bob_of(credit, struct m0_rm_loan, rl_credit, &loan_bob);
		M0_UT_ASSERT(loan != NULL && loan->rl_other != NULL);
		loan->rl_other->rem_session =
			&rm_ctx[csrv_id].rc_sess[dsrv_id];
	} m0_tl_endfor;
}

static void test2_verify(void)
{
	struct m0_rm_owner *so2 = &rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct m0_rm_owner *so1 = &rm_ctx[SERVER_1].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_sublet));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

static void test2_run(void)
{
	struct m0_rm_incoming *in = &rm_ctx[SERVER_1].rc_test_data.rd_in;

	/* Server-2 is upward creditor for Server-1 */
	creditor_cookie_setup(SERVER_1, SERVER_2);
	/*
	 * This request will get NENYA from Server-2 and DURIN from
	 * Server-3 via Server-2.
	 */
	credit_setup(SERVER_1, RIF_MAY_BORROW, NENYA | DURIN);
	m0_rm_credit_get(in);
	if (incoming_state(in) == RI_WAIT)
		m0_chan_wait(&rm_ctx[SERVER_1].rc_clink);
	M0_UT_ASSERT (incoming_state(in) == RI_SUCCESS);
	M0_UT_ASSERT(in->rin_rc == 0);
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void server1_tests(void)
{
	m0_chan_wait(&tests_clink[TEST2]);
	m0_clink_add_lock(&rm_ctx[SERVER_1].rc_chan,
			  &rm_ctx[SERVER_1].rc_clink);
	test2_run();
	test2_verify();
	m0_clink_del_lock(&rm_ctx[SERVER_1].rc_clink);

	m0_chan_signal_lock(&rr_tests_chan);
}

static void test3_verify(void)
{
	struct m0_rm_owner *so3 = &rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct m0_rm_owner *so2 = &rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct m0_rm_owner *so1 = &rm_ctx[SERVER_1].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so3->ro_sublet));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

static void test3_run(void)
{
	struct m0_rm_incoming *in = &rm_ctx[SERVER_2].rc_test_data.rd_in;

	/*
	 * 1. Test-case - Set LOCAL_WAIT flags. We should get the error
	 *                -EREMOTE as NENYA is now on SERVER_1.
	 */
	credit_setup(SERVER_2, RIF_LOCAL_WAIT, NENYA);
	m0_rm_credit_get(in);
	M0_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	M0_UT_ASSERT(in->rin_rc == -EREMOTE);
	m0_rm_incoming_fini(in);
	m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);

	/*
	 * 2. Test-case - NENYA is on SERVER_1. VILYA is on SERVER_3.
	 *                Make sure both borrow and revoke succeed in a
	 *                single request.
	 */
	loan_session_set(SERVER_2, SERVER_1);
	credit_setup(SERVER_2, RIF_MAY_REVOKE | RIF_MAY_BORROW,
		    NENYA | VILYA);
	m0_rm_credit_get(in);
	if (incoming_state(in) == RI_WAIT)
		m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	M0_UT_ASSERT(in->rin_rc == 0);
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void test1_verify(void)
{
	struct m0_rm_owner *so3 = &rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct m0_rm_owner *so2 = &rm_ctx[SERVER_2].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so3->ro_sublet));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
}

/*
 * Test borrow
 */
static void test1_run(void)
{
	struct m0_rm_incoming *in = &rm_ctx[SERVER_2].rc_test_data.rd_in;

	/*
	 * 1. Test-case - Set LOCAL_WAIT flags. We should get the error
	 *                -EREMOTE as NENYA is on SERVER_3.
	 */
	credit_setup(SERVER_2, RIF_LOCAL_WAIT, NENYA);
	m0_rm_credit_get(in);
	M0_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	M0_UT_ASSERT(in->rin_rc == -EREMOTE);
	m0_rm_incoming_fini(in);
	m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);

	/*
	 * 2. Test-case - Incorrect owner cookie. We should get the error
	 *                -EPROTO
	 */
	credit_setup(SERVER_2, RIF_MAY_BORROW, NENYA);
	m0_rm_credit_get(in);
	if (incoming_state(in) == RI_WAIT)
		m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	M0_UT_ASSERT(in->rin_rc == -EPROTO);
	m0_rm_incoming_fini(in);

	/*
	 * 3. Test-case - Setup creditor cookie. Credit request should
	 *                succeed.
	 */
	/* Server-3 is upward creditor for Server-2 */
	creditor_cookie_setup(SERVER_2, SERVER_3);
	credit_setup(SERVER_2, RIF_MAY_BORROW, NENYA);
	m0_rm_credit_get(in);
	if (incoming_state(in) == RI_WAIT)
		m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	M0_UT_ASSERT(in->rin_rc == 0);
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void server2_tests(void)
{
	m0_chan_wait(&tests_clink[TEST1]);
	m0_clink_add_lock(&rm_ctx[SERVER_2].rc_chan,
			  &rm_ctx[SERVER_2].rc_clink);
	test1_run();
	test1_verify();

	/* Begin next test */
	m0_chan_signal_lock(&rr_tests_chan);

	m0_chan_wait(&tests_clink[TEST3]);
	test3_run();
	test3_verify();
	m0_clink_del_lock(&rm_ctx[SERVER_2].rc_clink);

	/* Begin next test */
	m0_chan_signal_lock(&rr_tests_chan);
}

static void test4_run(void)
{
	struct m0_rm_owner *so3 = &rm_ctx[SERVER_3].rc_test_data.rd_owner;
	int		    rc;

	/*
	 * Tests m0_rm_owner_windup(). This tests automatic revokes.
	 */
	loan_session_set(SERVER_3, SERVER_2);
	m0_rm_owner_windup(so3);
	rc = m0_rm_owner_timedwait(so3, ROS_FINAL, M0_TIME_NEVER);
	M0_UT_ASSERT(rc == -ESRCH);
	M0_UT_ASSERT(owner_state(so3) == ROS_FINAL);
	m0_rm_owner_fini(so3);
	M0_SET0(&rm_ctx[SERVER_3].rc_test_data.rd_owner);
	m0_rm_owner_init(&rm_ctx[SERVER_3].rc_test_data.rd_owner,
			 &rm_ctx[SERVER_3].rc_test_data.rd_res.rs_resource,
			 NULL);
}

static void server3_tests(void)
{
	m0_chan_wait(&tests_clink[TEST4]);
	m0_clink_add_lock(&rm_ctx[SERVER_3].rc_chan,
			  &rm_ctx[SERVER_3].rc_clink);
	test4_run();
	m0_clink_del_lock(&rm_ctx[SERVER_3].rc_clink);
}

static void rm_server_start(const int tid)
{
	if (tid < SERVER_NR)
		server_start(tid);

	switch(tid) {
	case SERVER_1:
		server1_tests();
		break;
	case SERVER_2:
		server2_tests();
		break;
	case SERVER_3:
		server3_tests();
		break;
	default:
		break;
	}
}

/*
 * Configure server hierarchy.
 */
static void server_hier_config(void)
{
	rm_ctx[SERVER_1].creditor_id = SERVER_2;
	rm_ctx[SERVER_1].debtor_id = SERVER_INVALID;

	rm_ctx[SERVER_2].creditor_id = SERVER_3;
	rm_ctx[SERVER_2].debtor_id = SERVER_1;

	rm_ctx[SERVER_3].creditor_id = SERVER_INVALID;
	rm_ctx[SERVER_3].debtor_id = SERVER_2;
}

static void remote_credits_utinit(void)
{
	uint32_t i;

	for (i = 0; i < SERVER_NR; ++i) {
		M0_SET0(&rm_ctx[i]);
		rm_ctx[i].rc_ep_addr = serv_addr[i];
		rm_ctx[i].rc_id = i;
		rm_ctx[i].rc_cob_id.id = cob_ids[i];
		rm_ctx_init(&rm_ctx[i]);
	}
	server_hier_config();
	m0_mutex_init(&rr_tests_chan_mutex);
	m0_chan_init(&rr_tests_chan, &rr_tests_chan_mutex);
	/* Set up test sync points */
	for (i = 0; i < TEST_NR; ++i) {
		m0_clink_init(&tests_clink[i], NULL);
		m0_clink_add_lock(&rr_tests_chan, &tests_clink[i]);
	}
	m0_rm_fop_init();
}

static void remote_credits_utfini(void)
{
	uint32_t i;

	m0_rm_fop_fini();
	for (i = 0; i < SERVER_NR; ++i) {
		server_stop(i);
	}
	for (i = 0; i < SERVER_NR; ++i) {
		rm_ctx_fini(&rm_ctx[i]);
	}
	for (i = 0; i < TEST_NR; ++i) {
		m0_clink_del_lock(&tests_clink[i]);
		m0_clink_fini(&tests_clink[i]);
	}
	m0_chan_fini_lock(&rr_tests_chan);
	m0_mutex_fini(&rr_tests_chan_mutex);
}

void remote_credits_test(void)
{
	int rc;
	int i;

	remote_credits_utinit();
	/* Start RM servers */
	for (i = 0; i < SERVER_NR; ++i) {
		rc = M0_THREAD_INIT(&rm_ctx[i].rc_thr, int, NULL,
				    &rm_server_start, i, "rm_server_%d", i);
		M0_UT_ASSERT(rc == 0);
	}

	/* Now start the tests - wait till all the servers are ready */
	m0_chan_signal_lock(&rr_tests_chan);
	for (i = 0; i < SERVER_NR; ++i) {
		m0_thread_join(&rm_ctx[i].rc_thr);
		m0_thread_fini(&rm_ctx[i].rc_thr);
	}
	remote_credits_utfini();
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
