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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

#include "rpc/rpc_addb.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"     /* M0_IN */
#include "lib/types.h"
#include "lib/finject.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"
#include "rpc/service.h"

/**
 * @addtogroup rpc
 * @{
 */

M0_INTERNAL int m0_rpc__post_locked(struct m0_rpc_item *item,
				    struct m0_rpc_slot *slot);

struct m0_addb_ctx m0_rpc_addb_ctx;

M0_INTERNAL int m0_rpc_init(void)
{
	int rc;

	M0_ENTRY();

        m0_addb_ctx_type_register(&m0_addb_ct_rpc_mod);
        m0_addb_ctx_type_register(&m0_addb_ct_rpc_machine);
        m0_addb_ctx_type_register(&m0_addb_ct_rpc_frm);

        m0_addb_rec_type_register(&m0_addb_rt_rpc_stats_items);
        m0_addb_rec_type_register(&m0_addb_rt_rpc_stats_packets);
        m0_addb_rec_type_register(&m0_addb_rt_rpc_stats_bytes);
        m0_addb_rec_type_register(&m0_addb_rt_rpc_sent_item_sizes);
        m0_addb_rec_type_register(&m0_addb_rt_rpc_rcvd_item_sizes);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_rpc_addb_ctx,
			 &m0_addb_ct_rpc_mod, &m0_addb_proc_ctx);
	rc = m0_rpc_item_module_init() ?:
		m0_rpc_service_register() ?:
		m0_rpc_session_module_init();

	return M0_RC(rc);
}

M0_INTERNAL void m0_rpc_fini(void)
{
	M0_ENTRY();

	m0_rpc_session_module_fini();
	m0_rpc_service_unregister();
	m0_rpc_item_module_fini();
	m0_addb_ctx_fini(&m0_rpc_addb_ctx);

	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_post(struct m0_rpc_item *item)
{
	return m0_rpc_post_slot(item, NULL);
}

M0_INTERNAL int m0_rpc_post_slot(struct m0_rpc_item *item,
				 struct m0_rpc_slot *slot)
{
	int                    rc;
	struct m0_rpc_machine *machine;

	M0_ENTRY("item: %p", item);
	M0_PRE(item->ri_session != NULL);
	M0_PRE(m0_rpc_conn_is_snd(item->ri_session->s_conn));

	machine = session_machine(item->ri_session);

	M0_ASSERT(m0_rpc_item_size(item) <= machine->rm_min_recv_size);

	m0_rpc_machine_lock(machine);
	rc = m0_rpc__post_locked(item, slot);
	m0_rpc_machine_unlock(machine);

	return M0_RC(rc);
}
M0_EXPORTED(m0_rpc_post);

M0_INTERNAL int m0_rpc__post_locked(struct m0_rpc_item *item,
				    struct m0_rpc_slot *slot)
{
	struct m0_rpc_session  *session;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL && item->ri_type != NULL);
	M0_PRE(m0_rpc_item_is_request(item) && !m0_rpc_item_is_bound(item));

	session = item->ri_session;
	M0_ASSERT_EX(m0_rpc_session_invariant(session));
	M0_ASSERT(M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
						 M0_RPC_SESSION_BUSY)));
	M0_ASSERT(m0_rpc_item_size(item) <=
			m0_rpc_session_get_max_item_size(session));
	M0_ASSERT(m0_rpc_machine_is_locked(session_machine(session)));

	item->ri_rmachine = session_machine(session);
	item->ri_rpc_time = m0_time_now();
	m0_rpc_item_sm_init(item, M0_RPC_ITEM_OUTGOING);

	if (slot == NULL) {
		item->ri_stage = RPC_ITEM_STAGE_FUTURE;
		m0_rpc_item_send(item);
	} else {
		m0_rpc_slot_item_add(slot, item);
	}
	return M0_RC(item->ri_error);
}

int m0_rpc_reply_post(struct m0_rpc_item *request, struct m0_rpc_item *reply)
{
	struct m0_rpc_slot_ref *sref;
	struct m0_rpc_machine  *machine;
	struct m0_rpc_slot     *slot;

	M0_ENTRY("req_item: %p, rep_item: %p", request, reply);
	M0_PRE(request != NULL && reply != NULL);
	M0_PRE(request->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS);
	M0_PRE(request->ri_session != NULL);
	M0_PRE(reply->ri_type != NULL);
	M0_PRE(m0_rpc_item_size(reply) <=
			m0_rpc_session_get_max_item_size(request->ri_session));
	M0_PRE(m0_rpc_conn_is_rcv(request->ri_session->s_conn));

	if (M0_FI_ENABLED("delay_reply")) {
		M0_LOG(M0_DEBUG, "%p reply delayed", request);
		m0_nanosleep(m0_time(1, 200 * 1000 * 1000), NULL);
	}

	reply->ri_resend_interval = M0_TIME_NEVER;
	reply->ri_rpc_time = m0_time_now();
	reply->ri_session  = request->ri_session;
	machine = reply->ri_rmachine = request->ri_rmachine;
	/* BEWARE: structure instance copy ahead */
	reply->ri_slot_refs[0] = request->ri_slot_refs[0];
	sref = &reply->ri_slot_refs[0];
	/* don't need values of sr_link and sr_ready_link of request item */
	slot_item_tlink_init(reply);

	sref->sr_item = reply;

	reply->ri_prio     = request->ri_prio;
	reply->ri_deadline = 0;
	reply->ri_error    = 0;

	slot = sref->sr_slot;
	m0_rpc_machine_lock(machine);
	m0_rpc_item_sm_init(reply, M0_RPC_ITEM_OUTGOING);
	__slot_reply_received(slot, request, reply);
	m0_rpc_machine_unlock(machine);
	return M0_RC(0);
}
M0_EXPORTED(m0_rpc_reply_post);

M0_INTERNAL int m0_rpc_oneway_item_post(const struct m0_rpc_conn *conn,
					struct m0_rpc_item *item)
{
	struct m0_rpc_machine *machine;

	M0_ENTRY("conn: %p, item: %p", conn, item);
	M0_PRE(conn != NULL);
	M0_PRE(m0_rpc_machine_is_not_locked(conn->c_rpc_machine));

	machine = conn->c_rpc_machine;
	m0_rpc_machine_lock(machine);
	m0_rpc_oneway_item_post_locked(conn, item);
	m0_rpc_machine_unlock(machine);

	return M0_RC(0);
}

M0_INTERNAL void m0_rpc_oneway_item_post_locked(const struct m0_rpc_conn *conn,
						struct m0_rpc_item *item)
{
	M0_PRE(conn != NULL &&
	       m0_rpc_machine_is_locked(conn->c_rpc_machine));
	M0_PRE(item != NULL && m0_rpc_item_is_oneway(item));

	item->ri_resend_interval = M0_TIME_NEVER;
	item->ri_rpc_time        = m0_time_now();
	item->ri_rmachine        = conn->c_rpc_machine;

	m0_rpc_item_sm_init(item, M0_RPC_ITEM_OUTGOING);
	item->ri_nr_sent++;
	m0_rpc_frm_enq_item(&conn->c_rpcchan->rc_frm, item);
}

M0_INTERNAL int m0_rpc_reply_timedwait(struct m0_clink *clink,
				       const m0_time_t timeout)
{
	int rc;
	M0_ENTRY("timeout: [%llu:%llu]",
		 (unsigned long long)m0_time_seconds(timeout),
		 (unsigned long long) m0_time_nanoseconds(timeout));
	M0_PRE(clink != NULL);
	M0_PRE(m0_clink_is_armed(clink));

	rc = m0_chan_timedwait(clink, timeout) ? 0 : -ETIMEDOUT;
	return M0_RC(rc);
}
M0_EXPORTED(m0_rpc_reply_timedwait);


static void rpc_buffer_pool_low(struct m0_net_buffer_pool *bp)
{
	/* Buffer pool is below threshold.  */
}

static const struct m0_net_buffer_pool_ops b_ops = {
	.nbpo_not_empty	      = m0_net_domain_buffer_pool_not_empty,
	.nbpo_below_threshold = rpc_buffer_pool_low,
};

M0_INTERNAL int m0_rpc_net_buffer_pool_setup(struct m0_net_domain *ndom,
					     struct m0_net_buffer_pool
					     *app_pool, uint32_t bufs_nr,
					     uint32_t tm_nr)
{
	int	    rc;
	uint32_t    segs_nr;
	m0_bcount_t seg_size;

	M0_ENTRY("net_dom: %p", ndom);
	M0_PRE(ndom != NULL);
	M0_PRE(app_pool != NULL);
	M0_PRE(bufs_nr != 0);

	seg_size = m0_rpc_max_seg_size(ndom);
	segs_nr  = m0_rpc_max_segs_nr(ndom);
	app_pool->nbp_ops = &b_ops;
	rc = m0_net_buffer_pool_init(app_pool, ndom,
				     M0_NET_BUFFER_POOL_THRESHOLD,
				     segs_nr, seg_size, tm_nr, M0_SEG_SHIFT);
	if (rc != 0)
		return M0_ERR(rc, "net_buf_pool: Initialization");

	m0_net_buffer_pool_lock(app_pool);
	rc = m0_net_buffer_pool_provision(app_pool, bufs_nr);
	m0_net_buffer_pool_unlock(app_pool);

	return M0_RC(rc == bufs_nr ? 0 : -ENOMEM);
}
M0_EXPORTED(m0_rpc_net_buffer_pool_setup);

void m0_rpc_net_buffer_pool_cleanup(struct m0_net_buffer_pool *app_pool)
{
	M0_PRE(app_pool != NULL);
	m0_net_buffer_pool_fini(app_pool);
}
M0_EXPORTED(m0_rpc_net_buffer_pool_cleanup);

M0_INTERNAL uint32_t m0_rpc_bufs_nr(uint32_t len, uint32_t tms_nr)
{
	return len +
	       /* It is used so that more than one free buffer is present
		* for each TM when tms_nr > 8.
		*/
	       max32u(tms_nr / 4, 1) +
	       /* It is added so that frequent low_threshold callbacks of
		* buffer pool can be reduced.
		*/
	       M0_NET_BUFFER_POOL_THRESHOLD;
}

M0_INTERNAL m0_bcount_t m0_rpc_max_seg_size(struct m0_net_domain *ndom)
{
	M0_PRE(ndom != NULL);

	return min64u(m0_net_domain_get_max_buffer_segment_size(ndom),
		      M0_SEG_SIZE);
}

M0_INTERNAL uint32_t m0_rpc_max_segs_nr(struct m0_net_domain *ndom)
{
	M0_PRE(ndom != NULL);

	return m0_net_domain_get_max_buffer_size(ndom) /
	       m0_rpc_max_seg_size(ndom);
}

M0_INTERNAL m0_bcount_t m0_rpc_max_msg_size(struct m0_net_domain *ndom,
					    m0_bcount_t rpc_size)
{
	m0_bcount_t mbs;

	M0_PRE(ndom != NULL);

	mbs = m0_net_domain_get_max_buffer_size(ndom);
	return rpc_size != 0 ? min64u(mbs, max64u(rpc_size, M0_SEG_SIZE)) : mbs;
}

M0_INTERNAL uint32_t m0_rpc_max_recv_msgs(struct m0_net_domain *ndom,
					  m0_bcount_t rpc_size)
{
	M0_PRE(ndom != NULL);

	return m0_net_domain_get_max_buffer_size(ndom) /
	       m0_rpc_max_msg_size(ndom, rpc_size);
}

/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
