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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 08/21/2012
 */
#include "lib/types.h"            /* uint64_t */
#include "lib/chan.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/ut.h"
#include "db/db.h"
#include "cob/cob.h"
#include "net/lnet/lnet.h"
#include "net/buffer_pool.h"
#include "mdstore/mdstore.h"
#include "reqh/reqh.h"
#include "rpc/rpc.h"
#include "rpc/rpc_machine.h"
#include "fop/fom_generic.h"
#include "fop/fom_generic_ff.h"

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
	struct c2_thread	  rc_thr;
	struct c2_chan		  rc_chan;
	struct c2_clink		  rc_clink;
	struct c2_rpc_machine	  rc_rpc;
	struct c2_dbenv		  rc_dbenv;
	struct c2_fol		  rc_fol;
	struct c2_cob_domain_id	  rc_cob_id;
	struct c2_mdstore	  rc_mdstore;
	struct c2_cob_domain	  rc_cob_dom;
	struct c2_net_domain	  rc_net_dom;
	struct c2_net_buffer_pool rc_bufpool;
	struct c2_net_xprt	 *rc_xprt;
	struct c2_reqh		  rc_reqh;
	struct c2_net_end_point	 *rc_ep[SERVER_NR];
	struct c2_rpc_conn	  rc_conn[SERVER_NR];
	struct c2_rpc_session	  rc_sess[SERVER_NR];
	struct rm_ut_data	  rc_test_data;
};

static struct c2_chan rr_tests_chan;
static struct c2_clink tests_clink[TEST_NR];

struct rm_context rm_ctx[SERVER_NR];

static void buf_empty(struct c2_net_buffer_pool *bp);
static void buf_low(struct c2_net_buffer_pool *bp);

const struct c2_net_buffer_pool_ops buf_ops = {
	.nbpo_below_threshold = buf_low,
	.nbpo_not_empty	      = buf_empty,
};

static void buf_empty(struct c2_net_buffer_pool *bp)
{
}

static void server1_in_complete(struct c2_rm_incoming *in, int32_t rc)
{
	C2_UT_ASSERT(in != NULL);
	c2_chan_broadcast(&rm_ctx[SERVER_1].rc_chan);
}

static void server1_in_conflict(struct c2_rm_incoming *in)
{
}

const struct c2_rm_incoming_ops server1_incoming_ops = {
	.rio_complete = server1_in_complete,
	.rio_conflict = server1_in_conflict
};

static void server2_in_complete(struct c2_rm_incoming *in, int32_t rc)
{
	C2_UT_ASSERT(in != NULL);
	c2_chan_broadcast(&rm_ctx[SERVER_2].rc_chan);
}

static void server2_in_conflict(struct c2_rm_incoming *in)
{
}

const struct c2_rm_incoming_ops server2_incoming_ops = {
	.rio_complete = server2_in_complete,
	.rio_conflict = server2_in_conflict
};

static void server3_in_complete(struct c2_rm_incoming *in, int32_t rc)
{
	C2_UT_ASSERT(in != NULL);
	c2_chan_broadcast(&rm_ctx[SERVER_3].rc_chan);
}

static void server3_in_conflict(struct c2_rm_incoming *in)
{
}

const struct c2_rm_incoming_ops server3_incoming_ops = {
	.rio_complete = server3_in_complete,
	.rio_conflict = server3_in_conflict
};

static void buf_low(struct c2_net_buffer_pool *bp)
{
}

static void rm_ctx_init(struct rm_context *rmctx)
{
	int rc;

	rmctx->rc_xprt = &c2_net_lnet_xprt;

	rc = c2_net_domain_init(&rmctx->rc_net_dom, rmctx->rc_xprt);
	C2_UT_ASSERT(rc == 0);

	rmctx->rc_bufpool.nbp_ops = &buf_ops;
	rc = c2_rpc_net_buffer_pool_setup(&rmctx->rc_net_dom,
					  &rmctx->rc_bufpool,
					  bp_buf_nr, bp_tm_nr);
	C2_UT_ASSERT(rc == 0);

	rc = c2_dbenv_init(&rmctx->rc_dbenv, db_name[rmctx->rc_id], 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_fol_init(&rmctx->rc_fol, &rmctx->rc_dbenv);
        C2_UT_ASSERT(rc == 0);

	rc = c2_cob_domain_init(&rmctx->rc_cob_dom, &rmctx->rc_dbenv,
				&rmctx->rc_cob_id);
	C2_UT_ASSERT(rc == 0);

	rc = c2_mdstore_init(&rmctx->rc_mdstore, &rmctx->rc_cob_id,
			     &rmctx->rc_dbenv, 0);
	C2_UT_ASSERT(rc == 0);

	rc = c2_reqh_init(&rmctx->rc_reqh, (void *)1, &rmctx->rc_dbenv,
			  &rmctx->rc_mdstore, &rmctx->rc_fol, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_machine_init(&rmctx->rc_rpc, &rmctx->rc_cob_dom,
				 &rmctx->rc_net_dom, rmctx->rc_ep_addr,
				 &rmctx->rc_reqh, &rmctx->rc_bufpool,
				 C2_BUFFER_ANY_COLOUR,
				 C2_RPC_DEF_MAX_RPC_MSG_SIZE,
				 C2_NET_TM_RECV_QUEUE_DEF_LEN);
	C2_UT_ASSERT(rc == 0);
}

static void rm_ctx_fini(struct rm_context *rmctx)
{
	c2_rpc_machine_fini(&rmctx->rc_rpc);
	c2_reqh_fini(&rmctx->rc_reqh);
	c2_cob_domain_fini(&rmctx->rc_cob_dom);
	c2_fol_fini(&rmctx->rc_fol);
	c2_dbenv_fini(&rmctx->rc_dbenv);
	c2_rpc_net_buffer_pool_cleanup(&rmctx->rc_bufpool);
	c2_net_domain_fini(&rmctx->rc_net_dom);
}

static void rm_connect(struct rm_context *src, const struct rm_context *dest)
{
	struct c2_net_end_point *ep;
	int		         rc;

	/*
	 * Create a local end point to communicate with remote server.
	 */
	rc = c2_net_end_point_create(&ep,
				     &src->rc_rpc.rm_tm,
				     dest->rc_ep_addr);
	C2_UT_ASSERT(rc == 0);
	src->rc_ep[dest->rc_id] = ep;

	rc = c2_rpc_conn_create(&src->rc_conn[dest->rc_id],
				ep,
				&src->rc_rpc, 15, 10);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_session_create(&src->rc_sess[dest->rc_id],
				   &src->rc_conn[dest->rc_id], 1, 30);
	C2_UT_ASSERT(rc == 0);
}

static void rm_disconnect(struct rm_context *src, const struct rm_context *dest)
{
	int rc;

	rc = c2_rpc_session_destroy(&src->rc_sess[dest->rc_id], 30);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_conn_destroy(&src->rc_conn[dest->rc_id], 30);
	C2_UT_ASSERT(rc == 0);

	c2_net_end_point_put(src->rc_ep[dest->rc_id]);
	C2_UT_ASSERT(rc == 0);
}

static void server_1_setup()
{
	struct c2_rm_remote *creditor;

	rm_connect(&rm_ctx[SERVER_1], &rm_ctx[SERVER_2]);
	rm_utdata_init(&rm_ctx[SERVER_1].rc_test_data, OBJ_OWNER);

	C2_ALLOC_PTR(creditor);
	C2_UT_ASSERT(creditor != NULL);
	c2_rm_remote_init(creditor,
		       rm_ctx[SERVER_1].rc_test_data.rd_owner.ro_resource);
	creditor->rem_session = &rm_ctx[SERVER_1].rc_sess[SERVER_2];

	rm_ctx[SERVER_1].rc_test_data.rd_owner.ro_creditor = creditor;
	c2_chan_init(&rm_ctx[SERVER_1].rc_chan);
	c2_clink_init(&rm_ctx[SERVER_1].rc_clink, NULL);
}

static void server_2_setup()
{
	struct c2_rm_remote *creditor;

	rm_connect(&rm_ctx[SERVER_2], &rm_ctx[SERVER_3]);
	rm_connect(&rm_ctx[SERVER_2], &rm_ctx[SERVER_1]);
	rm_utdata_init(&rm_ctx[SERVER_2].rc_test_data, OBJ_OWNER);

	C2_ALLOC_PTR(creditor);
	C2_UT_ASSERT(creditor != NULL);
	c2_rm_remote_init(creditor,
		       rm_ctx[SERVER_2].rc_test_data.rd_owner.ro_resource);
	creditor->rem_session = &rm_ctx[SERVER_2].rc_sess[SERVER_3];

	rm_ctx[SERVER_2].rc_test_data.rd_owner.ro_creditor = creditor;
	c2_chan_init(&rm_ctx[SERVER_2].rc_chan);
	c2_clink_init(&rm_ctx[SERVER_2].rc_clink, NULL);
}

static void server_3_setup()
{
	rm_connect(&rm_ctx[SERVER_3], &rm_ctx[SERVER_2]);
	rm_utdata_init(&rm_ctx[SERVER_3].rc_test_data, OBJ_OWNER);
	rm_test_owner_capital_raise(&rm_ctx[SERVER_3].rc_test_data.rd_owner,
				    &rm_ctx[SERVER_3].rc_test_data.rd_right);
	c2_chan_init(&rm_ctx[SERVER_3].rc_chan);
	c2_clink_init(&rm_ctx[SERVER_3].rc_clink, NULL);
}

static void server1_stop()
{
	struct c2_rm_remote *creditor;

	rm_disconnect(&rm_ctx[SERVER_1], &rm_ctx[SERVER_2]);

	creditor = rm_ctx[SERVER_1].rc_test_data.rd_owner.ro_creditor;
	C2_UT_ASSERT(creditor != NULL);
	c2_rm_remote_fini(creditor);
	c2_free(creditor);
	rm_ctx[SERVER_1].rc_test_data.rd_owner.ro_creditor = NULL;

	c2_clink_fini(&rm_ctx[SERVER_2].rc_clink);
	c2_chan_fini(&rm_ctx[SERVER_1].rc_chan);
	rm_utdata_fini(&rm_ctx[SERVER_1].rc_test_data, OBJ_OWNER);
}

static void server2_stop()
{
	struct c2_rm_remote *creditor;

	rm_disconnect(&rm_ctx[SERVER_2], &rm_ctx[SERVER_3]);
	rm_disconnect(&rm_ctx[SERVER_2], &rm_ctx[SERVER_1]);

	creditor = rm_ctx[SERVER_2].rc_test_data.rd_owner.ro_creditor;
	C2_UT_ASSERT(creditor != NULL);
	c2_rm_remote_fini(creditor);
	c2_free(creditor);
	rm_ctx[SERVER_2].rc_test_data.rd_owner.ro_creditor = NULL;

	c2_clink_fini(&rm_ctx[SERVER_2].rc_clink);
	c2_chan_fini(&rm_ctx[SERVER_2].rc_chan);
	rm_utdata_fini(&rm_ctx[SERVER_2].rc_test_data, OBJ_OWNER);
}

static void server3_stop()
{
	rm_disconnect(&rm_ctx[SERVER_3], &rm_ctx[SERVER_2]);
	c2_clink_fini(&rm_ctx[SERVER_3].rc_clink);
	c2_chan_fini(&rm_ctx[SERVER_3].rc_chan);
	rm_utdata_fini(&rm_ctx[SERVER_3].rc_test_data, OBJ_OWNER);
}

static void creditor_cookie_setup(enum rm_server dsrv_id,
				  enum rm_server csrv_id)
{
	struct c2_rm_owner *creditor = &rm_ctx[csrv_id].rc_test_data.rd_owner;
	struct c2_rm_owner *owner = &rm_ctx[dsrv_id].rc_test_data.rd_owner;

	c2_cookie_init(&owner->ro_creditor->rem_cookie, &creditor->ro_id);
		
}

static void rm_servers_stop()
{
	server1_stop();
	server2_stop();
	server3_stop();
}

static void right_setup(enum rm_server srv_id,
			enum c2_rm_incoming_flags flag,
			int value)
{
	struct c2_rm_incoming *in = &rm_ctx[srv_id].rc_test_data.rd_in;
	struct c2_rm_owner    *owner = &rm_ctx[srv_id].rc_test_data.rd_owner;

	c2_rm_incoming_init(in, owner, C2_RIT_LOCAL, RIP_NONE, flag);
	c2_rm_right_init(&in->rin_want, owner);
	in->rin_want.ri_datum = value;
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
	struct c2_rm_owner *owner = &rm_ctx[csrv_id].rc_test_data.rd_owner;
	struct c2_rm_loan  *loan;
	struct c2_rm_right *right;

	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&owner->ro_sublet));
	c2_tl_for(c2_rm_ur, &owner->ro_sublet, right) {
		loan = bob_of(right, struct c2_rm_loan, rl_right, &loan_bob);
		C2_UT_ASSERT(loan != NULL && loan->rl_other != NULL);
		loan->rl_other->rem_session =
			&rm_ctx[csrv_id].rc_sess[dsrv_id];
	} c2_tl_endfor;
}

static void test2_verify()
{
	struct c2_rm_owner *so2 = &rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct c2_rm_owner *so1 = &rm_ctx[SERVER_1].rc_test_data.rd_owner;

	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so2->ro_sublet));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	C2_UT_ASSERT(c2_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

static void test2_run()
{
	struct c2_rm_incoming *in = &rm_ctx[SERVER_1].rc_test_data.rd_in;

	/* Server-2 is upward creditor for Server-1 */
	creditor_cookie_setup(SERVER_1, SERVER_2);
	right_setup(SERVER_1, RIF_MAY_BORROW, NENYA | DURIN);
	c2_rm_right_get(in);
	C2_UT_ASSERT (incoming_state(in) == RI_WAIT);
	c2_chan_wait(&rm_ctx[SERVER_1].rc_clink);
	C2_UT_ASSERT (incoming_state(in) == RI_SUCCESS);
	C2_UT_ASSERT(in->rin_rc == 0);
	c2_rm_right_put(in);
	c2_rm_incoming_fini(in);
}

static void server1_tests()
{
	c2_chan_wait(&tests_clink[TEST2]);
	c2_clink_add(&rm_ctx[SERVER_1].rc_chan, &rm_ctx[SERVER_1].rc_clink);
	test2_run();
	test2_verify();
	c2_clink_del(&rm_ctx[SERVER_1].rc_clink);

	c2_chan_signal(&rr_tests_chan);
}

static void test3_verify()
{
	struct c2_rm_owner *so3 = &rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct c2_rm_owner *so2 = &rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct c2_rm_owner *so1 = &rm_ctx[SERVER_1].rc_test_data.rd_owner;

	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so3->ro_sublet));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	C2_UT_ASSERT(c2_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	C2_UT_ASSERT(c2_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

static void test3_run()
{
	struct c2_rm_incoming *in = &rm_ctx[SERVER_2].rc_test_data.rd_in;

	/*
	 * 1. Test-case - Set LOCAL_WAIT flags. We should get the error
	 *                -EREMOTE as NENYA is now on SERVER_1.
	 */
	right_setup(SERVER_2, RIF_LOCAL_WAIT, NENYA);
	c2_rm_right_get(in);
	C2_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	C2_UT_ASSERT(in->rin_rc == -EREMOTE);
	c2_rm_incoming_fini(in);
	c2_chan_wait(&rm_ctx[SERVER_2].rc_clink);

	/*
	 * 2. Test-case - NENYA is on SERVER_1. VILYA is on SERVER_3.
	 *                Make sure both borrow and revoke succed in a
	 *                single request.
	 */
	loan_session_set(SERVER_2, SERVER_1);
	right_setup(SERVER_2, RIF_MAY_REVOKE | RIF_MAY_BORROW,
		    NENYA | VILYA);
	c2_rm_right_get(in);
	C2_UT_ASSERT(incoming_state(in) == RI_WAIT);
	c2_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	C2_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	C2_UT_ASSERT(in->rin_rc == 0);
	c2_rm_right_put(in);
	c2_rm_incoming_fini(in);
}

static void test1_verify()
{
	struct c2_rm_owner *so3 = &rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct c2_rm_owner *so2 = &rm_ctx[SERVER_2].rc_test_data.rd_owner;

	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so3->ro_sublet));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
}

/*
 * Test borrow
 */
static void test1_run()
{
	struct c2_rm_incoming *in = &rm_ctx[SERVER_2].rc_test_data.rd_in;

	/*
	 * 1. Test-case - Set LOCAL_WAIT flags. We should get the error
	 *                -EREMOTE as NENYA is on SERVER_3.
	 */
	right_setup(SERVER_2, RIF_LOCAL_WAIT, NENYA);
	c2_rm_right_get(in);
	C2_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	C2_UT_ASSERT(in->rin_rc == -EREMOTE);
	c2_rm_incoming_fini(in);
	c2_chan_wait(&rm_ctx[SERVER_2].rc_clink);

	/*
	 * 2. Test-case - Incorrect owner cookie. We should get the error
	 *                -EPROTO
	 */
	right_setup(SERVER_2, RIF_MAY_BORROW, NENYA);
	c2_rm_right_get(in);
	C2_UT_ASSERT(incoming_state(in) == RI_WAIT);
	c2_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	C2_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	C2_UT_ASSERT(in->rin_rc == -EPROTO);
	c2_rm_incoming_fini(in);

	/*
	 * 3. Test-case - Setup creditor cookie. Right request should
	 *                succeed.
	 */
	/* Server-3 is upward creditor for Server-2 */
	creditor_cookie_setup(SERVER_2, SERVER_3);
	right_setup(SERVER_2, RIF_MAY_BORROW, NENYA);
	c2_rm_right_get(in);
	C2_UT_ASSERT(incoming_state(in) == RI_WAIT);
	c2_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	C2_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	C2_UT_ASSERT(in->rin_rc == 0);
	c2_rm_right_put(in);
	c2_rm_incoming_fini(in);
}

static void server2_tests()
{
	c2_chan_wait(&tests_clink[TEST1]);
	c2_clink_add(&rm_ctx[SERVER_2].rc_chan, &rm_ctx[SERVER_2].rc_clink);
	test1_run();
	test1_verify();

	/* Begin next test */
	c2_chan_signal(&rr_tests_chan);

	c2_chan_wait(&tests_clink[TEST3]);
	test3_run();
	test3_verify();
	c2_clink_del(&rm_ctx[SERVER_2].rc_clink);

	/* Begin next test */
	c2_chan_signal(&rr_tests_chan);
}

static void test4_verify()
{
	struct c2_rm_owner *so3 = &rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct c2_rm_owner *so2 = &rm_ctx[SERVER_2].rc_test_data.rd_owner;

	C2_UT_ASSERT(c2_rm_ur_tlist_is_empty(&so3->ro_sublet));
	C2_UT_ASSERT(c2_rm_ur_tlist_is_empty(&so2->ro_sublet));
	C2_UT_ASSERT(c2_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	C2_UT_ASSERT(c2_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
}

static void test4_run()
{
	struct c2_rm_incoming *in = &rm_ctx[SERVER_3].rc_test_data.rd_in;

	right_setup(SERVER_3, RIF_MAY_REVOKE, NENYA | VILYA | DURIN);
	loan_session_set(SERVER_3, SERVER_2);
	c2_rm_right_get(in);
	C2_UT_ASSERT(incoming_state(in) == RI_WAIT);
	c2_chan_wait(&rm_ctx[SERVER_3].rc_clink);
	C2_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	C2_UT_ASSERT(in->rin_rc == 0);
	c2_rm_right_put(in);
	c2_rm_incoming_fini(in);
}

static void server3_tests()
{
	c2_chan_wait(&tests_clink[TEST4]);
	c2_clink_add(&rm_ctx[SERVER_3].rc_chan, &rm_ctx[SERVER_3].rc_clink);
	test4_run();
	test4_verify();
	c2_clink_del(&rm_ctx[SERVER_3].rc_clink);
}

static void rm_server_start(const int tid)
{
	switch(tid) {
	case SERVER_1:
		server_1_setup();
		server1_tests();
		break;
	case SERVER_2:
		server_2_setup();
		server2_tests();
		break;
	case SERVER_3:
		server_3_setup();
		server3_tests();
		break;
	default:
		break;
	}
}

static void remote_rights_utinit()
{
	uint32_t i;

	for (i = 0; i < SERVER_NR; ++i) {
		C2_SET0(&rm_ctx[i]);
		rm_ctx[i].rc_ep_addr = serv_addr[i];
		rm_ctx[i].rc_id = i;
		rm_ctx[i].rc_cob_id.id = cob_ids[i];
		rm_ctx_init(&rm_ctx[i]);
	}
	c2_chan_init(&rr_tests_chan);
	for (i = 0; i < TEST_NR; ++i) {
		c2_clink_init(&tests_clink[i], NULL);
		c2_clink_add(&rr_tests_chan, &tests_clink[i]);
	}
	c2_rm_fop_init();
}

static void remote_rights_utfini()
{
	uint32_t i;

	c2_rm_fop_fini();
	for (i = 0; i < SERVER_NR; ++i) {
		rm_ctx_fini(&rm_ctx[i]);
	}
	for (i = 0; i < TEST_NR; ++i) {
		c2_clink_del(&tests_clink[i]);
		c2_clink_fini(&tests_clink[i]);
	}
	c2_chan_fini(&rr_tests_chan);
}

void remote_rights_test()
{
	int rc;
	int i;

	remote_rights_utinit();

	for (i = 0; i < SERVER_NR; ++i) {
		rc = C2_THREAD_INIT(&rm_ctx[i].rc_thr, int, NULL,
				    &rm_server_start, i, "rm_server_%d", i);
		C2_UT_ASSERT(rc == 0);
	}

	c2_chan_signal(&rr_tests_chan);
	for (i = 0; i < SERVER_NR; ++i) {
		c2_thread_join(&rm_ctx[i].rc_thr);
		c2_thread_fini(&rm_ctx[i].rc_thr);
	}
	rm_servers_stop();
	remote_rights_utfini();
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
