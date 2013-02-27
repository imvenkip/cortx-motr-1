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
 * Original creation date: 06/28/2012
 */

/**
   @addtogroup rpc

   @{
 */

#include "rpc/rpc_addb.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/finject.h"       /* M0_FI_ENABLED */
#include "addb/addb.h"
#include "mero/magic.h"
#include "db/db.h"
#include "cob/cob.h"
#include "net/net.h"
#include "net/buffer_pool.h"   /* m0_net_buffer_pool_[lock|unlock] */
#include "reqh/reqh.h"
#include "rpc/rpc_internal.h"

#define RPCMC_ADDB_FUNCFAIL(rc, mc, loc, parentctx, ctx)		\
	M0_ADDB_FUNC_FAIL(mc, M0_RPC_ADDB_LOC_##loc, rc,		\
			  parentctx, ctx)

/* Forward declarations. */
static void rpc_tm_cleanup(struct m0_rpc_machine *machine);
static int rpc_tm_setup(struct m0_net_transfer_mc *tm,
			struct m0_net_domain      *net_dom,
			const char                *ep_addr,
			struct m0_net_buffer_pool *pool,
			uint32_t                   colour,
			m0_bcount_t                msg_size,
			uint32_t                   qlen);
static int __rpc_machine_init(struct m0_rpc_machine *machine);
static void __rpc_machine_fini(struct m0_rpc_machine *machine);
M0_INTERNAL void rpc_worker_thread_fn(struct m0_rpc_machine *machine);
static struct m0_rpc_chan *rpc_chan_locate(struct m0_rpc_machine *machine,
					   struct m0_net_end_point *dest_ep);
static int rpc_chan_create(struct m0_rpc_chan **chan,
			   struct m0_rpc_machine *machine,
			   struct m0_net_end_point *dest_ep,
			   uint64_t max_packets_in_flight);
static void rpc_chan_ref_release(struct m0_ref *ref);
static void rpc_recv_pool_buffer_put(struct m0_net_buffer *nb);
static void buf_recv_cb(const struct m0_net_buffer_event *ev);
static void net_buf_received(struct m0_net_buffer    *nb,
			     m0_bindex_t              offset,
			     m0_bcount_t              length,
			     struct m0_net_end_point *from_ep);
static void packet_received(struct m0_rpc_packet    *p,
			    struct m0_rpc_machine   *machine,
			    struct m0_net_end_point *from_ep);
static void item_received(struct m0_rpc_item      *item,
			  struct m0_net_end_point *from_ep);
static void net_buf_err(struct m0_net_buffer *nb, int32_t status);
static void drain_item_sources(struct m0_rpc_machine *machine);

static const struct m0_bob_type rpc_machine_bob_type = {
	.bt_name         = "rpc_machine",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_rpc_machine, rm_magix),
	.bt_magix        = M0_RPC_MACHINE_MAGIC,
	.bt_check        = NULL
};

M0_BOB_DEFINE(, &rpc_machine_bob_type, m0_rpc_machine);

/**
   Buffer callback for buffers added by rpc layer for receiving messages.
 */
static const struct m0_net_buffer_callbacks rpc_buf_recv_cb = {
	.nbc_cb = {
		[M0_NET_QT_MSG_RECV] = buf_recv_cb,
	}
};

M0_TL_DESCR_DEFINE(rpc_chan, "rpc_channels", static, struct m0_rpc_chan,
		   rc_linkage, rc_magic, M0_RPC_CHAN_MAGIC,
		   M0_RPC_CHAN_HEAD_MAGIC);
M0_TL_DEFINE(rpc_chan, static, struct m0_rpc_chan);

M0_TL_DESCR_DEFINE(rmach_watch, "rpc_machine_watch", M0_INTERNAL,
		   struct m0_rpc_machine_watch, mw_linkage, mw_magic,
		   M0_RPC_MACHINE_WATCH_MAGIC,
		   M0_RPC_MACHINE_WATCH_HEAD_MAGIC);
M0_TL_DEFINE(rmach_watch, M0_INTERNAL, struct m0_rpc_machine_watch);

M0_TL_DESCR_DEFINE(rpc_conn, "rpc-conn", M0_INTERNAL, struct m0_rpc_conn,
		   c_link, c_magic, M0_RPC_CONN_MAGIC, M0_RPC_CONN_HEAD_MAGIC);
M0_TL_DEFINE(rpc_conn, M0_INTERNAL, struct m0_rpc_conn);

static void rpc_tm_event_cb(const struct m0_net_tm_event *ev)
{
	/* Do nothing */
}

/**
    Transfer machine callback vector for transfer machines created by
    rpc layer.
 */
static struct m0_net_tm_callbacks m0_rpc_tm_callbacks = {
	.ntc_event_cb = rpc_tm_event_cb
};

M0_INTERNAL int m0_rpc_machine_init(struct m0_rpc_machine *machine,
				    struct m0_cob_domain *dom,
				    struct m0_net_domain *net_dom,
				    const char *ep_addr,
				    struct m0_reqh *reqh,
				    struct m0_net_buffer_pool *receive_pool,
				    uint32_t colour,
				    m0_bcount_t msg_size, uint32_t queue_len)
{
	int rc;

	M0_ENTRY("machine: %p, com_dom: %p, net_dom: %p, ep_addr: %s"
		 "reqh:%p", machine, dom, net_dom, (char *)ep_addr, reqh);
	M0_PRE(dom	    != NULL);
	M0_PRE(machine	    != NULL);
	M0_PRE(ep_addr	    != NULL);
	M0_PRE(net_dom	    != NULL);
	M0_PRE(receive_pool != NULL);

	if (M0_FI_ENABLED("fake_error"))
		M0_RETURN(-EINVAL);

	M0_SET0(machine);
	machine->rm_dom		  = dom;
	machine->rm_reqh	  = reqh;
	machine->rm_min_recv_size = m0_rpc_max_msg_size(net_dom, msg_size);

	rc = __rpc_machine_init(machine);
	if (rc != 0)
		M0_RETURN(rc);

	machine->rm_stopping = false;
	rc = M0_THREAD_INIT(&machine->rm_worker, struct m0_rpc_machine *,
			    NULL, &rpc_worker_thread_fn, machine, "rpc_worker");
	if (rc != 0)
		goto out_fini;

	rc = rpc_tm_setup(&machine->rm_tm, net_dom, ep_addr, receive_pool,
			  colour, msg_size, queue_len);
	if (rc != 0)
		goto out_stop_worker;

	M0_ASSERT(rc == 0);
	M0_RETURN(0);

out_stop_worker:
	machine->rm_stopping = true;
	m0_clink_signal(&machine->rm_sm_grp.s_clink);
	m0_thread_join(&machine->rm_worker);

out_fini:
	__rpc_machine_fini(machine);
	M0_ASSERT(rc != 0);
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_machine_init);

static int __rpc_machine_init(struct m0_rpc_machine *machine)
{
	int		    rc;
	struct m0_addb_mc  *addb_mc;
	struct m0_addb_ctx *ctx;

	M0_ENTRY("machine: %p", machine);
	rpc_chan_tlist_init(&machine->rm_chans);
	rpc_conn_tlist_init(&machine->rm_incoming_conns);
	rpc_conn_tlist_init(&machine->rm_outgoing_conns);
	rmach_watch_tlist_init(&machine->rm_watch);
	m0_rpc_services_tlist_init(&machine->rm_services);

	addb_mc = REQH_ADDB_MC_CONFIGURED(machine->rm_reqh) ?
		  &machine->rm_reqh->rh_addb_mc : &m0_addb_gmc;
	ctx     = machine->rm_reqh != NULL ?
		  &machine->rm_reqh->rh_addb_ctx : &m0_addb_proc_ctx;

	M0_ADDB_CTX_INIT(addb_mc, &machine->rm_addb_ctx,
			 &m0_addb_ct_rpc_machine, ctx);

	M0_SET0(&machine->rm_cntr_sent_item_sizes);
	M0_SET0(&machine->rm_cntr_rcvd_item_sizes);
	rc = m0_addb_counter_init(&machine->rm_cntr_sent_item_sizes,
				  &m0_addb_rt_rpc_sent_item_sizes);
	if (rc != 0)
		goto out_fini;

	rc = m0_addb_counter_init(&machine->rm_cntr_rcvd_item_sizes,
				  &m0_addb_rt_rpc_rcvd_item_sizes);
	if (rc != 0)
		goto cntr_fini;

	m0_sm_group_init(&machine->rm_sm_grp);
	m0_rpc_machine_bob_init(machine);
	m0_sm_group_init(&machine->rm_sm_grp);
	M0_LEAVE();
	M0_RETURN(0);

cntr_fini:
	m0_addb_counter_fini(&machine->rm_cntr_sent_item_sizes);
out_fini:
	m0_addb_ctx_fini(&machine->rm_addb_ctx);
	M0_RETURN(rc);
}

static void __rpc_machine_fini(struct m0_rpc_machine *machine)
{
	M0_ENTRY("machine %p", machine);

	m0_sm_group_fini(&machine->rm_sm_grp);

	m0_addb_counter_fini(&machine->rm_cntr_sent_item_sizes);
	m0_addb_counter_fini(&machine->rm_cntr_rcvd_item_sizes);
	m0_addb_ctx_fini(&machine->rm_addb_ctx);

	rmach_watch_tlist_fini(&machine->rm_watch);
	m0_rpc_services_tlist_fini(&machine->rm_services);
	rpc_conn_tlist_fini(&machine->rm_outgoing_conns);
	rpc_conn_tlist_fini(&machine->rm_incoming_conns);
	rpc_chan_tlist_fini(&machine->rm_chans);
	m0_rpc_machine_bob_fini(machine);

	M0_LEAVE();
}

void m0_rpc_machine_fini(struct m0_rpc_machine *machine)
{
	struct m0_rpc_machine_watch *watch;

	M0_ENTRY("machine: %p", machine);
	M0_PRE(machine != NULL);

	m0_rpc_machine_lock(machine);
	machine->rm_stopping = true;
	m0_clink_signal(&machine->rm_sm_grp.s_clink);
	m0_rpc_machine_unlock(machine);

	M0_LOG(M0_INFO, "Waiting for RPC worker to join");
	m0_thread_join(&machine->rm_worker);
	m0_thread_fini(&machine->rm_worker);

	m0_rpc_machine_lock(machine);
	M0_PRE(rpc_conn_tlist_is_empty(&machine->rm_outgoing_conns));
	/* RPC does not yet support finalising active connections */
	M0_PRE(rpc_conn_tlist_is_empty(&machine->rm_incoming_conns));
	m0_rpc_machine_unlock(machine);

	/* Detach watchers if any */
	m0_tl_for(rmach_watch, &machine->rm_watch, watch) {
		rmach_watch_tlink_del_fini(watch);
		if (watch->mw_mach_terminated != NULL)
			watch->mw_mach_terminated(watch);
	} m0_tl_endfor;

	rpc_tm_cleanup(machine);
	__rpc_machine_fini(machine);
	M0_LEAVE();
}
M0_EXPORTED(m0_rpc_machine_fini);

/* Not static because formation ut requires it. */
M0_INTERNAL void rpc_worker_thread_fn(struct m0_rpc_machine *machine)
{
	M0_ENTRY();
	M0_PRE(machine != NULL);

	while (true) {
		m0_rpc_machine_lock(machine);
		if (machine->rm_stopping) {
			m0_rpc_machine_unlock(machine);
			M0_LEAVE("RPC worker thread STOPPED");
			return;
		}
		m0_sm_asts_run(&machine->rm_sm_grp);
		drain_item_sources(machine);
		m0_rpc_machine_unlock(machine);
		m0_chan_timedwait(&machine->rm_sm_grp.s_clink,
				  m0_time_from_now(60, 0));
	}
}

static void drain_item_sources(struct m0_rpc_machine *machine)
{
	struct m0_rpc_item_source *source;
	struct m0_rpc_conn        *conn;
	struct m0_rpc_item        *item;
	m0_bcount_t                max_size;

	M0_ENTRY();

	max_size = machine->rm_min_recv_size - m0_rpc_item_onwire_header_size;

	M0_LOG(M0_DEBUG, "max_size: %llu", (unsigned long long)max_size);
	m0_tl_for(rpc_conn, &machine->rm_outgoing_conns, conn) {
		M0_LOG(M0_DEBUG, "conn: %p", conn);
		m0_tl_for(item_source, &conn->c_item_sources, source) {
			M0_LOG(M0_DEBUG, "source: %p", source);
			while (source->ris_ops->riso_has_item(source)) {
				item = source->ris_ops->riso_get_item(source,
								     max_size);
				if (item == NULL)
					break;
				M0_LOG(M0_DEBUG, "item: %p", item);
				m0_rpc_oneway_item_post_locked(conn, item);
			}
		} m0_tl_endfor;
	} m0_tl_endfor;

	M0_LEAVE();
}

static struct m0_rpc_machine *
tm_to_rpc_machine(const struct m0_net_transfer_mc *tm)
{
	return container_of(tm, struct m0_rpc_machine, rm_tm);
}

static int rpc_tm_setup(struct m0_net_transfer_mc *tm,
			struct m0_net_domain      *net_dom,
			const char                *ep_addr,
			struct m0_net_buffer_pool *pool,
			uint32_t                   colour,
			m0_bcount_t                msg_size,
			uint32_t                   qlen)
{
	struct m0_rpc_machine *machine;
	struct m0_clink	       tmwait;
	int		       rc;
	struct m0_addb_mc     *addb_mc;

	M0_ENTRY("tm: %p, net_dom: %p, ep_addr: %s", tm, net_dom,
		 (char *)ep_addr);
	M0_PRE(tm != NULL && net_dom != NULL && ep_addr != NULL);
	M0_PRE(pool != NULL);

	tm->ntm_state     = M0_NET_TM_UNDEFINED;
	tm->ntm_callbacks = &m0_rpc_tm_callbacks;

	machine = tm_to_rpc_machine(tm);
	addb_mc = REQH_ADDB_MC_CONFIGURED(machine->rm_reqh) ?
		  &machine->rm_reqh->rh_addb_mc : &m0_addb_gmc;

	rc = m0_net_tm_init(tm, net_dom, addb_mc, &machine->rm_addb_ctx);
	if (rc < 0)
		M0_RETERR(rc, "TM initialization");

	rc = m0_net_tm_pool_attach(tm, pool, &rpc_buf_recv_cb,
				   m0_rpc_max_msg_size(net_dom, msg_size),
				   m0_rpc_max_recv_msgs(net_dom, msg_size),
				   qlen);
	if (rc < 0) {
		m0_net_tm_fini(tm);
		M0_RETERR(rc, "m0_net_tm_pool_attach");
	}

	m0_net_tm_colour_set(tm, colour);

	/* Start the transfer machine so that users of this rpc_machine
	   can send/receive messages. */
	m0_clink_init(&tmwait, NULL);
	m0_clink_add_lock(&tm->ntm_chan, &tmwait);

	rc = m0_net_tm_start(tm, ep_addr);
	if (rc == 0) {
		while (tm->ntm_state != M0_NET_TM_STARTED &&
		       tm->ntm_state != M0_NET_TM_FAILED)
			m0_chan_wait(&tmwait);
	}
	m0_clink_del_lock(&tmwait);
	m0_clink_fini(&tmwait);

	if (tm->ntm_state == M0_NET_TM_FAILED) {
		/** @todo Find more appropriate err code.
		    tm does not report cause of failure.
		 */
		rc = -ENETUNREACH;
		m0_net_tm_fini(tm);
		M0_RETERR(rc, "TM start");
	}
	M0_RETURN(rc);
}

static void rpc_tm_cleanup(struct m0_rpc_machine *machine)
{
	int			   rc;
	struct m0_clink		   tmwait;
	struct m0_net_transfer_mc *tm = &machine->rm_tm;

	M0_ENTRY("machine: %p", machine);
	M0_PRE(machine != NULL);

	m0_clink_init(&tmwait, NULL);
	m0_clink_add_lock(&tm->ntm_chan, &tmwait);

	rc = m0_net_tm_stop(tm, true);

	if (rc < 0) {
		m0_clink_del_lock(&tmwait);
		m0_clink_fini(&tmwait);
		RPCMC_ADDB_FUNCFAIL(rc, machine->rm_reqh != NULL ?
				    &machine->rm_reqh->rh_addb_mc :
				    &m0_addb_gmc, MACHINE_RPC_TM_CLEANUP,
				    machine->rm_reqh != NULL ?
				    &machine->rm_reqh->rh_addb_ctx :
				    &m0_addb_proc_ctx, &machine->rm_addb_ctx);
		M0_LOG(M0_ERROR, "TM stopping: FAILED with err: %d", rc);
		M0_LEAVE();
		return;
	}
	/* Wait for transfer machine to stop. */
	while (tm->ntm_state != M0_NET_TM_STOPPED &&
	       tm->ntm_state != M0_NET_TM_FAILED)
		m0_chan_wait(&tmwait);
	m0_clink_del_lock(&tmwait);
	m0_clink_fini(&tmwait);

	/* Fini the transfer machine here and deallocate the chan. */
	m0_net_tm_fini(tm);
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_machine_lock(struct m0_rpc_machine *machine)
{
	M0_PRE(machine != NULL);
	m0_sm_group_lock(&machine->rm_sm_grp);
}

M0_INTERNAL void m0_rpc_machine_unlock(struct m0_rpc_machine *machine)
{
	M0_PRE(machine != NULL);
	m0_sm_group_unlock(&machine->rm_sm_grp);
}

M0_INTERNAL bool m0_rpc_machine_is_locked(const struct m0_rpc_machine *machine)
{
	M0_PRE(machine != NULL);
	return m0_mutex_is_locked(&machine->rm_sm_grp.s_lock);
}
M0_EXPORTED(m0_rpc_machine_is_locked);

M0_INTERNAL bool
m0_rpc_machine_is_not_locked(const struct m0_rpc_machine *machine)
{
	M0_PRE(machine != NULL);
	return m0_mutex_is_not_locked(&machine->rm_sm_grp.s_lock);
}
M0_EXPORTED(m0_rpc_machine_is_not_locked);

static void __rpc_machine_get_stats(struct m0_rpc_machine *machine,
				    struct m0_rpc_stats *stats, bool reset)
{
	M0_PRE(machine != NULL);
	M0_PRE(m0_rpc_machine_is_locked(machine));
	M0_PRE(stats != NULL);

	*stats = machine->rm_stats;
	if (reset)
		M0_SET0(&machine->rm_stats);
}

void m0_rpc_machine_get_stats(struct m0_rpc_machine *machine,
			      struct m0_rpc_stats *stats, bool reset)
{
	M0_PRE(machine != NULL);

	m0_rpc_machine_lock(machine);
	__rpc_machine_get_stats(machine, stats, reset);
	m0_rpc_machine_unlock(machine);
}
M0_EXPORTED(m0_rpc_machine_get_stats);

static void __rpc_machine_stats_post_addb(struct m0_rpc_machine *machine)
{
	struct m0_addb_ctx  *cv[2];
	struct m0_rpc_stats  rm_stats;
	struct m0_addb_mc   *addb_mc;

	M0_PRE(m0_rpc_machine_is_locked(machine));

	cv[0] = &machine->rm_addb_ctx;
	cv[1] = NULL;

	addb_mc = machine->rm_reqh != NULL ?
		  &machine->rm_reqh->rh_addb_mc :
		  &m0_addb_gmc;
	if (m0_addb_counter_nr(&machine->rm_cntr_sent_item_sizes) > 0)
		M0_ADDB_POST_CNTR(addb_mc, cv,
				  &machine->rm_cntr_sent_item_sizes);

	if (m0_addb_counter_nr(&machine->rm_cntr_rcvd_item_sizes) > 0)
		M0_ADDB_POST_CNTR(addb_mc, cv,
				  &machine->rm_cntr_rcvd_item_sizes);

	__rpc_machine_get_stats(machine, &rm_stats, true);
	if (rm_stats.rs_nr_rcvd_items +
	    rm_stats.rs_nr_sent_items +
	    rm_stats.rs_nr_failed_items +
	    rm_stats.rs_nr_dropped_items +
	    rm_stats.rs_nr_timedout_items != 0)
		M0_ADDB_POST(addb_mc, &m0_addb_rt_rpc_stats_items, cv,
			     rm_stats.rs_nr_rcvd_items,
			     rm_stats.rs_nr_sent_items,
			     rm_stats.rs_nr_failed_items,
			     rm_stats.rs_nr_dropped_items,
			     rm_stats.rs_nr_timedout_items);

	if (rm_stats.rs_nr_rcvd_packets +
	    rm_stats.rs_nr_sent_packets +
	    rm_stats.rs_nr_failed_packets != 0)
		M0_ADDB_POST(addb_mc, &m0_addb_rt_rpc_stats_packets, cv,
			     rm_stats.rs_nr_rcvd_packets,
			     rm_stats.rs_nr_sent_packets,
			     rm_stats.rs_nr_failed_packets);

	if (rm_stats.rs_nr_sent_bytes +
	    rm_stats.rs_nr_rcvd_bytes != 0)
		M0_ADDB_POST(addb_mc, &m0_addb_rt_rpc_stats_bytes, cv,
			     rm_stats.rs_nr_sent_bytes,
			     rm_stats.rs_nr_rcvd_bytes);
}

M0_INTERNAL void m0_rpc_machine_stats_post_addb(struct m0_rpc_machine *machine)
{
	M0_PRE(machine != NULL);

	m0_rpc_machine_lock(machine);
	__rpc_machine_stats_post_addb(machine);
	m0_rpc_machine_unlock(machine);
}
M0_EXPORTED(m0_rpc_machine_stats_post_addb);

M0_INTERNAL void m0_rpc_machine_add_conn(struct m0_rpc_machine *rmach,
					 struct m0_rpc_conn    *conn)
{
	struct m0_rpc_machine_watch *watch;
	struct m0_tl                *tlist;

	M0_ENTRY("rmach: %p conn: %p", rmach, conn);

	M0_PRE(m0_rpc_machine_is_locked(rmach));
	M0_PRE(conn != NULL && !rpc_conn_tlink_is_in(conn));
	M0_PRE(equi(conn->c_flags & RCF_SENDER_END,
		    !(conn->c_flags & RCF_RECV_END)));

	tlist = (conn->c_flags & RCF_SENDER_END) ? &rmach->rm_outgoing_conns :
						   &rmach->rm_incoming_conns;
	rpc_conn_tlist_add(tlist, conn);
	M0_LOG(M0_DEBUG, "rmach %p conn %p added to %s list", rmach, conn,
		(conn->c_flags & RCF_SENDER_END) ? "outgoing" : "incoming");
	m0_tl_for(rmach_watch, &rmach->rm_watch, watch) {
		if (watch->mw_conn_added != NULL)
			watch->mw_conn_added(watch, conn);
	} m0_tl_endfor;

	M0_POST(rpc_conn_tlink_is_in(conn));
	M0_LEAVE();
}

M0_INTERNAL struct m0_rpc_chan *rpc_chan_get(struct m0_rpc_machine *machine,
					     struct m0_net_end_point *dest_ep,
					     uint64_t max_packets_in_flight)
{
	struct m0_rpc_chan	*chan;

	M0_ENTRY("machine: %p, max_packets_in_flight: %llu", machine,
		 (unsigned long long)max_packets_in_flight);
	M0_PRE(dest_ep != NULL);
	M0_PRE(m0_rpc_machine_is_locked(machine));

	if (M0_FI_ENABLED("fake_error"))
		return NULL;

	if (M0_FI_ENABLED("do_nothing"))
		return (struct m0_rpc_chan *)1;

	chan = rpc_chan_locate(machine, dest_ep);
	if (chan == NULL)
		rpc_chan_create(&chan, machine, dest_ep, max_packets_in_flight);

	M0_LEAVE("chan: %p", chan);
	return chan;
}

static struct m0_rpc_chan *rpc_chan_locate(struct m0_rpc_machine *machine,
					   struct m0_net_end_point *dest_ep)
{
	struct m0_rpc_chan *chan;
	bool                found;

	M0_ENTRY("machine: %p, dest_ep_addr: %s", machine,
		 (char *)dest_ep->nep_addr);
	M0_PRE(dest_ep != NULL);
	M0_PRE(m0_rpc_machine_is_locked(machine));

	found = false;
	/* Locate the chan from rpc_machine->chans list. */
	m0_tl_for(rpc_chan, &machine->rm_chans, chan) {
		M0_ASSERT(chan->rc_destep->nep_tm->ntm_dom ==
			  dest_ep->nep_tm->ntm_dom);
		if (chan->rc_destep == dest_ep) {
			m0_ref_get(&chan->rc_ref);
			m0_net_end_point_get(chan->rc_destep);
			found = true;
			break;
		}
	} m0_tl_endfor;

	M0_LEAVE("rc: %p", found ? chan : NULL);
	return found ? chan : NULL;
}

static int rpc_chan_create(struct m0_rpc_chan **chan,
			   struct m0_rpc_machine *machine,
			   struct m0_net_end_point *dest_ep,
			   uint64_t max_packets_in_flight)
{
	struct m0_rpc_frm_constraints  constraints;
	struct m0_net_domain          *ndom;
	struct m0_rpc_chan            *ch;

	M0_ENTRY("machine: %p", machine);
	M0_PRE(chan != NULL);
	M0_PRE(dest_ep != NULL);

	M0_PRE(m0_rpc_machine_is_locked(machine));

	M0_ALLOC_PTR_ADDB(ch, machine->rm_reqh != NULL ?
			  &machine->rm_reqh->rh_addb_mc : &m0_addb_gmc,
			  M0_RPC_ADDB_LOC_MACHINE_RPC_CHAN_CREATE,
			  &m0_addb_proc_ctx, &machine->rm_addb_ctx);
	if (ch == NULL) {
		*chan = NULL;
		M0_RETURN(-ENOMEM);
	}

	ch->rc_rpc_machine = machine;
	ch->rc_destep = dest_ep;
	m0_ref_init(&ch->rc_ref, 1, rpc_chan_ref_release);
	m0_net_end_point_get(dest_ep);

	ndom = machine->rm_tm.ntm_dom;

	constraints.fc_max_nr_packets_enqed = max_packets_in_flight;
	constraints.fc_max_packet_size = machine->rm_min_recv_size;
	constraints.fc_max_nr_bytes_accumulated =
				constraints.fc_max_packet_size;
	constraints.fc_max_nr_segments =
				m0_net_domain_get_max_buffer_segments(ndom);

	m0_rpc_frm_init(&ch->rc_frm, &constraints, &m0_rpc_frm_default_ops);
	rpc_chan_tlink_init_at(ch, &machine->rm_chans);
	*chan = ch;
	M0_RETURN(0);
}

M0_INTERNAL void rpc_chan_put(struct m0_rpc_chan *chan)
{
	struct m0_rpc_machine *machine;

	M0_ENTRY();
	M0_PRE(chan != NULL);

	if (M0_FI_ENABLED("do_nothing"))
		return;

	machine = chan->rc_rpc_machine;
	M0_PRE(m0_rpc_machine_is_locked(machine));

	m0_net_end_point_put(chan->rc_destep);
	m0_ref_put(&chan->rc_ref);
	M0_LEAVE();
}

static void rpc_chan_ref_release(struct m0_ref *ref)
{
	struct m0_rpc_chan *chan;

	M0_ENTRY();
	M0_PRE(ref != NULL);

	chan = container_of(ref, struct m0_rpc_chan, rc_ref);
	M0_ASSERT(chan != NULL);
	M0_ASSERT(m0_rpc_machine_is_locked(chan->rc_rpc_machine));

	rpc_chan_tlist_del(chan);
	m0_rpc_frm_fini(&chan->rc_frm);
	m0_free(chan);
	M0_LEAVE();
}

static void buf_recv_cb(const struct m0_net_buffer_event *ev)
{
	struct m0_net_buffer *nb;
	bool                  buf_is_queued;

	M0_ENTRY();
	nb = ev->nbe_buffer;
	M0_PRE(nb != NULL);

	if (ev->nbe_status == 0) {
		net_buf_received(nb, ev->nbe_offset, ev->nbe_length,
				 ev->nbe_ep);
	} else {
		if (ev->nbe_status != -ECANCELED)
			net_buf_err(nb, ev->nbe_status);
	}
	buf_is_queued = (nb->nb_flags & M0_NET_BUF_QUEUED);
	if (!buf_is_queued)
		rpc_recv_pool_buffer_put(nb);
	M0_LEAVE();
}

static void net_buf_received(struct m0_net_buffer    *nb,
			     m0_bindex_t              offset,
			     m0_bcount_t              length,
			     struct m0_net_end_point *from_ep)
{
	struct m0_rpc_machine *machine;
	struct m0_rpc_packet   p;
	int                    rc;

	M0_ENTRY("net_buf: %p, offset: %llu, length: %llu,"
		 "ep_addr: %s", nb, (unsigned long long)offset,
		 (unsigned long long)length, (char *)from_ep->nep_addr);

	machine = tm_to_rpc_machine(nb->nb_tm);
	m0_rpc_packet_init(&p);
	rc = m0_rpc_packet_decode(&p, &nb->nb_buffer, offset, length);
	if (rc != 0)
		RPCMC_ADDB_FUNCFAIL(rc, machine->rm_reqh != NULL ?
				    &machine->rm_reqh->rh_addb_mc :
				    &m0_addb_gmc, MACHINE_NET_BUF_RECEIVED,
				    machine->rm_reqh != NULL ?
				    &machine->rm_reqh->rh_addb_ctx :
				    &m0_addb_proc_ctx, &machine->rm_addb_ctx);

	/* There might be items in packet p, which were successfully decoded
	   before an error occured. */
	packet_received(&p, machine, from_ep);
	m0_rpc_packet_fini(&p);
	M0_LEAVE();
}

static void packet_received(struct m0_rpc_packet    *p,
			    struct m0_rpc_machine   *machine,
			    struct m0_net_end_point *from_ep)
{
	struct m0_rpc_item *item;

	M0_ENTRY();

	machine->rm_stats.rs_nr_rcvd_packets++;
	machine->rm_stats.rs_nr_rcvd_bytes += p->rp_size;
	/* packet p can also be empty */
	for_each_item_in_packet(item, p) {
		item->ri_rmachine = machine;
		m0_rpc_item_get(item);
		m0_rpc_packet_remove_item(p, item);
		item_received(item, from_ep);
		m0_rpc_machine_lock(machine); /* protect ri_sm fini() */
		m0_rpc_item_put(item);
		m0_rpc_machine_unlock(machine);
	} end_for_each_item_in_packet;

	M0_LEAVE();
}

static void item_received(struct m0_rpc_item      *item,
			  struct m0_net_end_point *from_ep)
{
	struct m0_rpc_machine *machine = item->ri_rmachine;
	int                    rc;

	M0_ENTRY("machine: %p, item: %p, ep_addr: %s", machine,
		 item, (char *)from_ep->nep_addr);

	if (M0_FI_ENABLED("drop_item")) {
		M0_LOG(M0_DEBUG, "item: %p [%s/%u] dropped", item,
			item_kind(item), item->ri_type->rit_opcode);
		return;
	}
	if (m0_rpc_item_is_conn_establish(item))
		m0_rpc_fop_conn_establish_ctx_init(item, from_ep);

	item->ri_rpc_time = m0_time_now();

	m0_rpc_machine_lock(machine);
	m0_rpc_item_sm_init(item, M0_RPC_ITEM_INCOMING);
	rc = m0_rpc_item_received(item, machine);
	if (rc == 0) {
		m0_rpc_item_change_state(item, M0_RPC_ITEM_ACCEPTED);
	} else {
		M0_LOG(M0_DEBUG, "%p [%s/%d] dropped", item, item_kind(item),
		       item->ri_type->rit_opcode);
		machine->rm_stats.rs_nr_dropped_items++;
	}
	m0_rpc_machine_unlock(machine);

	M0_LEAVE();
}

static void net_buf_err(struct m0_net_buffer *nb, int32_t status)
{
	struct m0_rpc_machine *machine;

	machine = tm_to_rpc_machine(nb->nb_tm);
	RPCMC_ADDB_FUNCFAIL(status, machine->rm_reqh != NULL ?
			   &machine->rm_reqh->rh_addb_mc : &m0_addb_gmc,
			   MACHINE_NET_BUF_ERR,
			   machine->rm_reqh != NULL ?
			   &machine->rm_reqh->rh_addb_ctx : &m0_addb_proc_ctx,
			   &machine->rm_addb_ctx);
}

/* Put buffer back into the pool */
static void rpc_recv_pool_buffer_put(struct m0_net_buffer *nb)
{
	struct m0_net_transfer_mc *tm;

	M0_ENTRY("net_buf: %p", nb);
	M0_PRE(nb != NULL);
	tm = nb->nb_tm;

	M0_PRE(tm != NULL);
	M0_PRE(tm->ntm_recv_pool != NULL && nb->nb_pool != NULL);
	M0_PRE(tm->ntm_recv_pool == nb->nb_pool);

	nb->nb_ep = NULL;
	m0_net_buffer_pool_lock(tm->ntm_recv_pool);
	m0_net_buffer_pool_put(tm->ntm_recv_pool, nb,
			       tm->ntm_pool_colour);
	m0_net_buffer_pool_unlock(tm->ntm_recv_pool);

	M0_LEAVE();
}

/*
 * RPC machine watch.
 */

void m0_rpc_machine_watch_attach(struct m0_rpc_machine_watch *watch)
{
	struct m0_rpc_machine *rmach;

	M0_ENTRY("watch: %p", watch);
	M0_PRE(watch != NULL);

	rmach = watch->mw_mach;
	M0_PRE(rmach != NULL);
	M0_PRE(m0_rpc_machine_is_not_locked(rmach));

	m0_rpc_machine_lock(rmach);
	rmach_watch_tlink_init_at_tail(watch, &rmach->rm_watch);
	m0_rpc_machine_unlock(rmach);

	M0_LEAVE();
}

void m0_rpc_machine_watch_detach(struct m0_rpc_machine_watch *watch)
{
	struct m0_rpc_machine *rmach;

	M0_ENTRY("watch: %p", watch);
	M0_PRE(watch != NULL);

	rmach = watch->mw_mach;
	M0_PRE(rmach != NULL);
	M0_PRE(m0_rpc_machine_is_not_locked(rmach));

	m0_rpc_machine_lock(rmach);
	if (rmach_watch_tlink_is_in(watch))
		rmach_watch_tlink_del_fini(watch);
	m0_rpc_machine_unlock(rmach);

	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

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
