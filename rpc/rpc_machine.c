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

#include "rpc/rpc.h"          /* m0_rpc_max_msg_size, m0_rpc_max_recv_msgs */
#include "rpc/rpc_internal.h"

/* Forward declarations. */
static void rpc_tm_cleanup(struct m0_rpc_machine *machine);
static int rpc_tm_setup(struct m0_net_transfer_mc *tm,
			struct m0_net_domain      *net_dom,
			const char                *ep_addr,
			struct m0_net_buffer_pool *pool,
			uint32_t                   colour,
			m0_bcount_t                msg_size,
			uint32_t                   qlen);
static void __rpc_machine_init(struct m0_rpc_machine *machine);
static void __rpc_machine_fini(struct m0_rpc_machine *machine);
static void conn_list_fini(struct m0_tl *list);
M0_INTERNAL void rpc_worker_thread_fn(struct m0_rpc_machine *machine);
static struct m0_rpc_chan *rpc_chan_locate(struct m0_rpc_machine *machine,
					   struct m0_net_end_point *dest_ep);
static int rpc_chan_create(struct m0_rpc_chan **chan,
			   struct m0_rpc_machine *machine,
			   struct m0_net_end_point *dest_ep,
			   uint64_t max_packets_in_flight);
static void rpc_chan_ref_release(struct m0_ref *ref);
static void rpc_recv_pool_buffer_put(struct m0_net_buffer *nb);
static void net_buf_event_handler(const struct m0_net_buffer_event *ev);
static void net_buf_received(struct m0_net_buffer    *nb,
			     m0_bindex_t              offset,
			     m0_bcount_t              length,
			     struct m0_net_end_point *from_ep);
static void packet_received(struct m0_rpc_packet    *p,
			    struct m0_rpc_machine   *machine,
			    struct m0_net_end_point *from_ep);
static void item_received(struct m0_rpc_item      *item,
			  struct m0_rpc_machine   *machine,
			  struct m0_net_end_point *from_ep);
static void net_buf_err(struct m0_net_buffer *nb, int32_t status);


/* ADDB Instrumentation for rpccore. */
static const struct m0_addb_ctx_type rpc_machine_addb_ctx_type = {
        .act_name = "rpc-machine"
};

const struct m0_addb_loc m0_rpc_machine_addb_loc = {
        .al_name = "rpc-machine"
};

M0_ADDB_EV_DEFINE_PUBLIC(m0_rpc_machine_func_fail, "rpc_machine_func_fail",
			 M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);

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
const struct m0_net_buffer_callbacks m0_rpc_rcv_buf_callbacks = {
	.nbc_cb = {
		[M0_NET_QT_MSG_RECV] = net_buf_event_handler,
	}
};

M0_TL_DESCR_DEFINE(rpc_chan, "rpc_channels", static, struct m0_rpc_chan,
		   rc_linkage, rc_magic, M0_RPC_CHAN_MAGIC,
		   M0_RPC_CHAN_HEAD_MAGIC);
M0_TL_DEFINE(rpc_chan, static, struct m0_rpc_chan);

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

static void rmachine_addb_failure(struct m0_rpc_machine *machine,
				  const char            *msg,
				  int                    rc)
{
	M0_ADDB_ADD(&machine->rm_addb, &m0_rpc_machine_addb_loc,
		    m0_rpc_machine_func_fail, msg, rc);
}

M0_INTERNAL int m0_rpc_machine_init(struct m0_rpc_machine *machine,
				    struct m0_cob_domain *dom,
				    struct m0_net_domain *net_dom,
				    const char *ep_addr,
				    struct m0_reqh *reqh,
				    struct m0_net_buffer_pool *receive_pool,
				    uint32_t colour,
				    m0_bcount_t msg_size, uint32_t queue_len)
{
	int		rc;

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

	__rpc_machine_init(machine);

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

static void __rpc_machine_init(struct m0_rpc_machine *machine)
{
	M0_ENTRY("machine: %p", machine);
	rpc_chan_tlist_init(&machine->rm_chans);
	rpc_conn_tlist_init(&machine->rm_incoming_conns);
	rpc_conn_tlist_init(&machine->rm_outgoing_conns);
	m0_rpc_services_tlist_init(&machine->rm_services);
	m0_addb_ctx_init(&machine->rm_addb, &rpc_machine_addb_ctx_type,
			 &m0_addb_global_ctx);
	m0_sm_group_init(&machine->rm_sm_grp);
	m0_rpc_machine_bob_init(machine);
	m0_sm_group_init(&machine->rm_sm_grp);
	M0_LEAVE();
}

static void __rpc_machine_fini(struct m0_rpc_machine *machine)
{
	M0_ENTRY("machine %p", machine);

	m0_sm_group_fini(&machine->rm_sm_grp);
	m0_addb_ctx_fini(&machine->rm_addb);
	m0_rpc_services_tlist_fini(&machine->rm_services);
	rpc_conn_tlist_fini(&machine->rm_outgoing_conns);
	rpc_conn_tlist_fini(&machine->rm_incoming_conns);
	rpc_chan_tlist_fini(&machine->rm_chans);
	m0_rpc_machine_bob_fini(machine);

	M0_LEAVE();
}

void m0_rpc_machine_fini(struct m0_rpc_machine *machine)
{
	M0_ENTRY("machine: %p", machine);
	M0_PRE(machine != NULL);

	m0_rpc_machine_lock(machine);
	machine->rm_stopping = true;
	m0_clink_signal(&machine->rm_sm_grp.s_clink);
	m0_rpc_machine_unlock(machine);

	M0_LOG(M0_INFO, "Waiting for RPC worker to join");
	m0_thread_join(&machine->rm_worker);

	m0_rpc_machine_lock(machine);
	M0_PRE(rpc_conn_tlist_is_empty(&machine->rm_outgoing_conns));
	conn_list_fini(&machine->rm_incoming_conns);
	m0_rpc_machine_unlock(machine);

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
		m0_rpc_machine_unlock(machine);
		m0_chan_timedwait(&machine->rm_sm_grp.s_clink,
				  m0_time_from_now(60, 0));
	}
}

static int rpc_tm_setup(struct m0_net_transfer_mc *tm,
			struct m0_net_domain      *net_dom,
			const char                *ep_addr,
			struct m0_net_buffer_pool *pool,
			uint32_t                   colour,
			m0_bcount_t                msg_size,
			uint32_t                   qlen)
{
	struct m0_clink tmwait;
	int             rc;

	M0_ENTRY("tm: %p, net_dom: %p, ep_addr: %s", tm, net_dom,
		 (char *)ep_addr);
	M0_PRE(tm != NULL && net_dom != NULL && ep_addr != NULL);
	M0_PRE(pool != NULL);

	tm->ntm_state     = M0_NET_TM_UNDEFINED;
	tm->ntm_callbacks = &m0_rpc_tm_callbacks;

	rc = m0_net_tm_init(tm, net_dom);
	if (rc < 0)
		M0_RETERR(rc, "TM initialization");

	rc = m0_net_tm_pool_attach(tm, pool,
				   &m0_rpc_rcv_buf_callbacks,
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
	m0_clink_add(&tm->ntm_chan, &tmwait);

	rc = m0_net_tm_start(tm, ep_addr);
	if (rc == 0) {
		while (tm->ntm_state != M0_NET_TM_STARTED &&
		       tm->ntm_state != M0_NET_TM_FAILED)
			m0_chan_wait(&tmwait);
	}
	m0_clink_del(&tmwait);
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
	m0_clink_add(&tm->ntm_chan, &tmwait);

	rc = m0_net_tm_stop(tm, true);

	if (rc < 0) {
		m0_clink_del(&tmwait);
		m0_clink_fini(&tmwait);
		M0_ADDB_ADD(&machine->rm_addb, &m0_rpc_machine_addb_loc,
			    m0_rpc_machine_func_fail, "m0_net_tm_stop", rc);
		M0_LOG(M0_ERROR, "TM stopping: FAILED with err: %d", rc);
		M0_LEAVE();
		return;
	}
	/* Wait for transfer machine to stop. */
	while (tm->ntm_state != M0_NET_TM_STOPPED &&
	       tm->ntm_state != M0_NET_TM_FAILED)
		m0_chan_wait(&tmwait);
	m0_clink_del(&tmwait);
	m0_clink_fini(&tmwait);

	/* Fini the transfer machine here and deallocate the chan. */
	m0_net_tm_fini(tm);
	M0_LEAVE();
}

/**
   XXX Temporary. This routine will be discarded, once rpc-core starts
   providing m0_rpc_item::ri_ops::rio_sent() callback.

   In-memory state of conn should be cleaned up when reply to CONN_TERMINATE
   has been sent. As of now, rpc-core does not provide this callback. So this
   is a temporary routine, that cleans up all terminated connections from
   rpc connection list maintained in rpc_machine.
 */
static void conn_list_fini(struct m0_tl *list)
{
        struct m0_rpc_conn *conn;

	M0_ENTRY();
        M0_PRE(list != NULL);

	m0_tl_for(rpc_conn, list, conn) {
                m0_rpc_conn_terminate_reply_sent(conn);
        } m0_tl_endfor;
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

void m0_rpc_machine_get_stats(struct m0_rpc_machine *machine,
			      struct m0_rpc_stats *stats, bool reset)
{
	M0_PRE(machine != NULL);
	M0_PRE(stats != NULL);

	m0_rpc_machine_lock(machine);
	*stats = machine->rm_stats;
	if(reset)
		M0_SET0(&machine->rm_stats);
	m0_rpc_machine_unlock(machine);
}
M0_EXPORTED(m0_rpc_machine_get_stats);

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

	M0_ALLOC_PTR_ADDB(ch, &machine->rm_addb, &m0_rpc_machine_addb_loc);
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

static void net_buf_event_handler(const struct m0_net_buffer_event *ev)
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

static struct m0_rpc_machine *
tm_to_rpc_machine(const struct m0_net_transfer_mc *tm)
{
	return container_of(tm, struct m0_rpc_machine, rm_tm);
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
		rmachine_addb_failure(machine, "Buffer decode failed", rc);
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
		item_received(item, machine, from_ep);
		m0_rpc_item_put(item);
	} end_for_each_item_in_packet;

	M0_LEAVE();
}

static void item_received(struct m0_rpc_item      *item,
			  struct m0_rpc_machine   *machine,
			  struct m0_net_end_point *from_ep)
{
	int rc;

	M0_ENTRY("machine: %p, item: %p, ep_addr: %s", machine,
		 item, (char *)from_ep->nep_addr);

	if (m0_rpc_item_is_conn_establish(item))
		m0_rpc_fop_conn_establish_ctx_init(item, from_ep, machine);

	item->ri_rpc_time = m0_time_now();

	m0_rpc_machine_lock(machine);
	m0_rpc_item_sm_init(item, &machine->rm_sm_grp, M0_RPC_ITEM_INCOMING);
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
	rmachine_addb_failure(machine, "Buffer event reported failure", status);
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
