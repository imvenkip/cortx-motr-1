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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 18-Apr-2013
 */

#include "ut/ut.h"
#include "ut/be.h"
#include "ut/ut_rpc_machine.h"

extern int m0_rpc_root_session_cob_create(struct m0_cob_domain *dom,
                                          struct m0_db_tx      *tx);

enum {
	UT_BUF_NR = 8,
	UT_TM_NR  = 2,
};

static void buf_dummy(struct m0_net_buffer_pool *bp);

const struct m0_net_buffer_pool_ops buf_ops = {
	.nbpo_below_threshold = buf_dummy,
	.nbpo_not_empty       = buf_dummy,
};

static void buf_dummy(struct m0_net_buffer_pool *bp)
{
}

#ifndef __KERNEL__

M0_INTERNAL void m0_ut_rpc_mach_init_and_add(struct m0_ut_rpc_mach_ctx *ctx)
{
	struct m0_sm_group    *grp;
	struct m0_be_seg      *seg;
	struct m0_be_tx        tx;
	struct m0_be_tx_credit cred = {};
	int                    rc;

	ctx->rmc_xprt = &m0_net_lnet_xprt;
	rc = m0_net_xprt_init(ctx->rmc_xprt);
	M0_ASSERT(rc == 0);

	rc = m0_net_domain_init(&ctx->rmc_net_dom, ctx->rmc_xprt,
				&m0_addb_proc_ctx);
	M0_ASSERT(rc == 0);

	ctx->rmc_bufpool.nbp_ops = &buf_ops;
	rc = m0_rpc_net_buffer_pool_setup(&ctx->rmc_net_dom, &ctx->rmc_bufpool,
					  UT_BUF_NR, UT_TM_NR);
	M0_ASSERT(rc == 0);

	/*
	 * Instead of using m0d and dealing with network, database and
	 * other subsystems, request handler is initialised in a 'special way'.
	 * This allows it to operate in a 'limited mode' which is enough for
	 * this test.
	 */

	rc = M0_REQH_INIT(&ctx->rmc_reqh,
			  .rhia_dtm       = NULL,
			  .rhia_db        = NULL,
			  .rhia_mdstore   = &ctx->rmc_mdstore,
			  .rhia_fol       = NULL,
			  .rhia_svc       = (void*)1);
	M0_ASSERT(rc == 0);

	m0_ut_backend_init(&ctx->rmc_ut_be, &ctx->rmc_ut_seg);
	seg = &ctx->rmc_ut_seg.bus_seg;
	grp = m0_be_ut_backend_sm_group_lookup(&ctx->rmc_ut_be);
	rc = m0_be_seg_dict_create(seg, grp);
	M0_ASSERT(rc == 0);

	rc = m0_reqh_dbenv_init(&ctx->rmc_reqh, seg, true);
	M0_ASSERT(rc == 0);

	rc = m0_mdstore_init(&ctx->rmc_mdstore, &ctx->rmc_cob_id, seg, 0);
	M0_ASSERT(rc == -ENOENT);
        rc = m0_mdstore_create(&ctx->rmc_mdstore, grp);
        M0_ASSERT(rc == 0);

	m0_cob_tx_credit(&ctx->rmc_mdstore.md_dom, M0_COB_OP_DOMAIN_MKFS, &cred);
	m0_ut_be_tx_begin(&tx, &ctx->rmc_ut_be, &cred);
	rc = m0_cob_domain_mkfs(&ctx->rmc_mdstore.md_dom, &M0_COB_SLASH_FID,
				&M0_COB_SESSIONS_FID, &tx);
	m0_ut_be_tx_end(&tx);
	M0_ASSERT(rc == 0);

	m0_reqh_start(&ctx->rmc_reqh);

	rc = m0_rpc_machine_init(&ctx->rmc_rpc,
				 &ctx->rmc_net_dom, ctx->rmc_ep_addr,
				 &ctx->rmc_reqh, &ctx->rmc_bufpool,
				 M0_BUFFER_ANY_COLOUR,
				 M0_RPC_DEF_MAX_RPC_MSG_SIZE,
				 M0_NET_TM_RECV_QUEUE_DEF_LEN);
	M0_ASSERT(rc == 0);

	m0_reqh_rpc_mach_tlink_init_at_tail(&ctx->rmc_rpc,
					    &ctx->rmc_reqh.rh_rpc_machines);
}

M0_INTERNAL void m0_ut_rpc_mach_fini(struct m0_ut_rpc_mach_ctx *ctx)
{
	struct m0_sm_group *grp;
	int                 rc;

	m0_reqh_rpc_mach_tlink_del_fini(&ctx->rmc_rpc);
	m0_rpc_machine_fini(&ctx->rmc_rpc);

	if (m0_reqh_state_get(&ctx->rmc_reqh) == M0_REQH_ST_NORMAL)
		m0_reqh_shutdown(&ctx->rmc_reqh);
	m0_reqh_dbenv_fini(&ctx->rmc_reqh, true);

	m0_reqh_services_terminate(&ctx->rmc_reqh);

	grp = m0_be_ut_backend_sm_group_lookup(&ctx->rmc_ut_be);
	rc = m0_mdstore_destroy(&ctx->rmc_mdstore, grp);
	M0_ASSERT(rc == 0);
	m0_mdstore_fini(&ctx->rmc_mdstore);

	rc = m0_be_seg_dict_destroy(&ctx->rmc_ut_seg.bus_seg, grp);
	M0_ASSERT(rc == 0);
	m0_ut_backend_fini(&ctx->rmc_ut_be, &ctx->rmc_ut_seg);

	m0_reqh_fom_domain_idle_wait(&ctx->rmc_reqh);
	M0_ASSERT(m0_reqh_state_get(&ctx->rmc_reqh) == M0_REQH_ST_STOPPED);
	m0_reqh_fini(&ctx->rmc_reqh);

	m0_rpc_net_buffer_pool_cleanup(&ctx->rmc_bufpool);
	m0_net_domain_fini(&ctx->rmc_net_dom);
	m0_net_xprt_fini(ctx->rmc_xprt);
}

#else

M0_INTERNAL void m0_ut_rpc_mach_init_and_add(struct m0_ut_rpc_mach_ctx *ctx)
{
	int rc;

	ctx->rmc_xprt = &m0_net_lnet_xprt;
	rc = m0_net_xprt_init(ctx->rmc_xprt);
	M0_ASSERT(rc == 0);

	rc = m0_net_domain_init(&ctx->rmc_net_dom, ctx->rmc_xprt,
				&m0_addb_proc_ctx);
	M0_ASSERT(rc == 0);

	ctx->rmc_bufpool.nbp_ops = &buf_ops;
	rc = m0_rpc_net_buffer_pool_setup(&ctx->rmc_net_dom, &ctx->rmc_bufpool,
					  UT_BUF_NR, UT_TM_NR);
	M0_ASSERT(rc == 0);

	rc = M0_REQH_INIT(&ctx->rmc_reqh,
			  .rhia_dtm       = (void*)1,
			  .rhia_db        = NULL,
			  .rhia_mdstore   = (void*)1,
			  .rhia_fol       = NULL,
			  .rhia_svc       = (void*)1);
	M0_ASSERT(rc == 0);
	m0_reqh_start(&ctx->rmc_reqh);
	rc = m0_rpc_machine_init(&ctx->rmc_rpc,
				 &ctx->rmc_net_dom, ctx->rmc_ep_addr,
				 &ctx->rmc_reqh, &ctx->rmc_bufpool,
				 M0_BUFFER_ANY_COLOUR,
				 M0_RPC_DEF_MAX_RPC_MSG_SIZE,
				 M0_NET_TM_RECV_QUEUE_DEF_LEN);
	M0_ASSERT(rc == 0);

	m0_reqh_rpc_mach_tlink_init_at_tail(&ctx->rmc_rpc,
					    &ctx->rmc_reqh.rh_rpc_machines);
}

M0_INTERNAL void m0_ut_rpc_mach_fini(struct m0_ut_rpc_mach_ctx *ctx)
{
	m0_reqh_services_terminate(&ctx->rmc_reqh);
	m0_reqh_rpc_mach_tlink_del_fini(&ctx->rmc_rpc);
	m0_rpc_machine_fini(&ctx->rmc_rpc);
	m0_reqh_fini(&ctx->rmc_reqh);

}

#endif

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
