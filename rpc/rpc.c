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

/**
 * @addtogroup rpc
 * @{
 */

M0_INTERNAL int m0_rpc__post_locked(struct m0_rpc_item *item);

struct m0_addb_ctx m0_rpc_addb_ctx;

static int rpc_service_start(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL);
	return 0;
}

static void rpc_service_stop(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL);
}

static void rpc_service_fini(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL);
	m0_free(service);
}

static const struct m0_reqh_service_ops rpc_ops = {
	.rso_start = rpc_service_start,
	.rso_stop = rpc_service_stop,
	.rso_fini = rpc_service_fini
};

static int rpc_service_allocate(struct m0_reqh_service **service,
				struct m0_reqh_service_type *stype,
				const char *arg __attribute__((unused)))
{
	struct m0_reqh_service *serv;

	M0_PRE(stype != NULL && service != NULL);

	RPC_ALLOC_PTR(serv, SERVICE_ALLOC, NULL);
	if (serv == NULL)
		return -ENOMEM;

	serv->rs_type = stype;
	serv->rs_ops = &rpc_ops;

	*service = serv;

	return 0;
}

static const struct m0_reqh_service_type_ops rpc_service_type_ops = {
	.rsto_service_allocate = rpc_service_allocate
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_rpc_service_type, &rpc_service_type_ops,
			    "rpcservice", &m0_addb_ct_rpc_serv);

M0_INTERNAL int m0_rpc_service_register(void)
{
	int rc;
	m0_addb_ctx_type_register(&m0_addb_ct_rpc_serv);
	rc = m0_reqh_service_type_register(&m0_rpc_service_type);
	return rc;
}

M0_INTERNAL void m0_rpc_service_unregister(void)
{
	m0_reqh_service_type_unregister(&m0_rpc_service_type);
}

M0_INTERNAL int m0_rpc_init(void)
{
	int rc;

	M0_ENTRY();

#undef CT_REG
#define CT_REG(n) m0_addb_ctx_type_register(&m0_addb_ct_rpc_##n)
        CT_REG(mod);
        CT_REG(machine);
        CT_REG(frm);
#undef CT_REG
#undef RT_REG
#define RT_REG(n) m0_addb_rec_type_register(&m0_addb_rt_rpc_##n)
        RT_REG(stats_items);
        RT_REG(stats_packets);
        RT_REG(stats_bytes);
        RT_REG(sent_item_sizes);
        RT_REG(rcvd_item_sizes);
#undef RT_REG
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_rpc_addb_ctx,
			 &m0_addb_ct_rpc_mod, &m0_addb_proc_ctx);
	rc = m0_rpc_item_type_list_init() ?:
	     m0_rpc_service_module_init() ?:
	     m0_rpc_service_register() ?:
	     m0_rpc_session_module_init();

	M0_RETURN(rc);
}

M0_INTERNAL void m0_rpc_fini(void)
{
	M0_ENTRY();

	m0_rpc_session_module_fini();
	m0_rpc_service_unregister();
	m0_rpc_service_module_fini();
	m0_rpc_item_type_list_fini();
	m0_addb_ctx_fini(&m0_rpc_addb_ctx);

	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_post(struct m0_rpc_item *item)
{
	int                    rc;
	uint64_t               size;
	struct m0_rpc_machine *machine;

	M0_ENTRY("item: %p", item);
	M0_PRE(item->ri_session != NULL);

	machine = session_machine(item->ri_session);

	size = m0_rpc_item_size(item);
	M0_ASSERT(size <= machine->rm_min_recv_size);

	m0_rpc_machine_lock(machine);
	rc = m0_rpc__post_locked(item);
	m0_rpc_machine_unlock(machine);

	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_post);

M0_INTERNAL int m0_rpc__post_locked(struct m0_rpc_item *item)
{
	struct m0_rpc_session  *session;
	struct m0_addb_counter *counter;
	int                     rc;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL && item->ri_type != NULL);
	/* XXX Temporary assertion, until bound item posting is supported */
	M0_PRE(m0_rpc_item_is_request(item) && !m0_rpc_item_is_bound(item));

	session = item->ri_session;
	M0_ASSERT(m0_rpc_session_invariant(session));
	M0_ASSERT(M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
						 M0_RPC_SESSION_BUSY)));
	M0_ASSERT(m0_rpc_item_size(item) <=
			m0_rpc_session_get_max_item_size(session));
	M0_ASSERT(m0_rpc_machine_is_locked(session_machine(session)));

	item->ri_rmachine = session_machine(session);
	item->ri_rpc_time = m0_time_now();
	item->ri_stage = RPC_ITEM_STAGE_FUTURE;
	m0_rpc_item_sm_init(item, M0_RPC_ITEM_OUTGOING);
	rc = m0_rpc_item_start_timer(item);
	if (rc == 0) {
		counter = &item->ri_rmachine->rm_cntr_sent_item_sizes;
		m0_addb_counter_update(counter,
				       (uint64_t)m0_rpc_item_size(item));
		m0_rpc_item_send(item);
	} else
		m0_rpc_item_failed(item, rc);
	M0_RETURN(rc);
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

	if (M0_FI_ENABLED("delay_reply")) {
		M0_LOG(M0_FATAL, "%p reply delayed", request);
		m0_nanosleep(m0_time(1, 200 * 1000 * 1000), NULL);
	}

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
	/*
	 * This hold will be released when the item is SENT or FAILED.
	 * See rpc/frmops.c:item_done() and m0_rpc_item_failed()
	 */
	m0_rpc_session_hold_busy(reply->ri_session);
	__slot_reply_received(slot, request, reply);
	m0_rpc_machine_unlock(machine);
	M0_RETURN(0);
}
M0_EXPORTED(m0_rpc_reply_post);

M0_INTERNAL int m0_rpc_oneway_item_post(const struct m0_rpc_conn *conn,
					struct m0_rpc_item *item)
{
	struct m0_rpc_machine *machine;

	M0_ENTRY("conn: %p, item: %p", conn, item);
	M0_PRE(conn != NULL);
	M0_PRE(item != NULL && m0_rpc_item_is_oneway(item));

	item->ri_rpc_time = m0_time_now();

	machine = item->ri_rmachine = conn->c_rpc_machine;
	m0_rpc_machine_lock(machine);
	m0_rpc_item_sm_init(item, M0_RPC_ITEM_OUTGOING);
	item->ri_nr_sent++;
	m0_rpc_frm_enq_item(&conn->c_rpcchan->rc_frm, item);
	m0_rpc_machine_unlock(machine);
	M0_RETURN(0);
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
	M0_RETURN(rc);
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
		M0_RETERR(rc, "net_buf_pool: Initialization");

	m0_net_buffer_pool_lock(app_pool);
	rc = m0_net_buffer_pool_provision(app_pool, bufs_nr);
	m0_net_buffer_pool_unlock(app_pool);

	M0_RETURN(rc == bufs_nr ? 0 : -ENOMEM);
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
