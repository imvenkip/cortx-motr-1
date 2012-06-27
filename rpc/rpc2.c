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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cob/cob.h"
#include "rpc/rpc2.h"
#include "rpc/rpcdbg.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/types.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "rpc/session.h"
#include "rpc/session_internal.h"
#include "rpc/service.h"    /* c2_rpc_services_tlist_.* */
#include "fop/fop.h"
#include "rpc/formation.h"
#include "fid/fid.h"
#include "reqh/reqh.h"
#include "rpc/rpc_onwire.h"
#include "fop/fop_item_type.h"
#include "lib/arith.h"
#include "lib/vec.h"
#include "lib/finject.h"
#include "rpc/formation2.h"

/* Forward declarations. */
static void rpc_net_buf_received(const struct c2_net_buffer_event *ev);
static void rpc_tm_cleanup(struct c2_rpc_machine *machine);

extern void frm_net_buffer_sent(const struct c2_net_buffer_event *ev);
extern void rpcobj_exit_stats_set(const struct c2_rpc *rpcobj,
		struct c2_rpc_machine *mach, enum c2_rpc_item_path path);

static void frm_worker_fn(struct c2_rpc_machine *machine);

int c2_rpc__post_locked(struct c2_rpc_item *item);

const struct c2_addb_ctx_type c2_rpc_addb_ctx_type = {
	.act_name = "rpc"
};

const struct c2_addb_loc c2_rpc_addb_loc = {
	.al_name = "rpc"
};

struct c2_addb_ctx c2_rpc_addb_ctx;

/* ADDB Instrumentation for rpccore. */
static const struct c2_addb_ctx_type rpc_machine_addb_ctx_type = {
	        .act_name = "rpc-machine"
};

const struct c2_addb_loc c2_rpc_machine_addb_loc = {
	        .al_name = "rpc-machine"
};

C2_ADDB_EV_DEFINE_PUBLIC(c2_rpc_machine_func_fail, "rpc_machine_func_fail",
		                C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
   Buffer callback for buffers added by rpc layer for receiving messages.
 */
const struct c2_net_buffer_callbacks c2_rpc_rcv_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV] = rpc_net_buf_received,
	}
};

/**
   Callback for net buffer used in posting
 */
const struct c2_net_buffer_callbacks c2_rpc_send_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_SEND] = frm_net_buffer_sent,
	}
};

static void rpc_tm_event_cb(const struct c2_net_tm_event *ev)
{
}

int c2_rpc_module_init(void)
{
	c2_addb_ctx_init(&c2_rpc_addb_ctx, &c2_rpc_addb_ctx_type,
			 &c2_addb_global_ctx);
	return 0;
}

void c2_rpc_module_fini(void)
{
	c2_addb_ctx_fini(&c2_rpc_addb_ctx);
}

/**
    Transfer machine callback vector for transfer machines created by
    rpc layer.
 */
static struct c2_net_tm_callbacks c2_rpc_tm_callbacks = {
	       .ntc_event_cb = rpc_tm_event_cb
};

void c2_rpcobj_fini(struct c2_rpc *rpc)
{
	rpc->r_session = NULL;
	c2_list_fini(&rpc->r_items);
	c2_list_link_fini(&rpc->r_linkage);
}

int c2_rpc_post(struct c2_rpc_item *item)
{
	struct c2_rpc_machine *machine;
	int                    rc;
	uint64_t	       item_size;

	C2_PRE(item->ri_session != NULL);

	machine	  = item->ri_session->s_conn->c_rpc_machine;
	item_size = item->ri_type->rit_ops->rito_item_size(item);

	c2_rpc_machine_lock(machine);
	C2_ASSERT(item_size <= machine->rm_min_recv_size);
	rc = c2_rpc__post_locked(item);
	c2_rpc_machine_unlock(machine);

	return rc;
}
C2_EXPORTED(c2_rpc_post);

int c2_rpc__post_locked(struct c2_rpc_item *item)
{
	struct c2_rpc_session *session;

	C2_ASSERT(item != NULL && item->ri_type != NULL);

	/*
	 * It is mandatory to specify item_ops, because rpc layer needs
	 * implementation of c2_rpc_item_ops::rio_free() in order to free the
	 * item. Consumer can use c2_fop_default_item_ops if, it is not
	 * interested in implementing other (excluding ->rio_free())
	 * interfaces of c2_rpc_item_ops. See also c2_fop_item_free().
	 */
	C2_ASSERT(item->ri_ops != NULL && item->ri_ops->rio_free != NULL);

	session = item->ri_session;
	C2_ASSERT(c2_rpc_session_invariant(session));
	C2_ASSERT(C2_IN(session->s_state, (C2_RPC_SESSION_IDLE,
					   C2_RPC_SESSION_BUSY)));
	C2_ASSERT(c2_rpc_item_size(item) <=
			c2_rpc_session_get_max_item_size(session));
	C2_ASSERT(c2_rpc_machine_is_locked(session->s_conn->c_rpc_machine));
	/*
	 * This hold will be released when the item is SENT or FAILED.
	 * See rpc/frmops.c:item_done()
	 */
	c2_rpc_session_hold_busy(session);

	item->ri_rpc_time = c2_time_now();

	item->ri_state = RPC_ITEM_SUBMITTED;
	c2_rpc_frm_enq_item(&item->ri_session->s_conn->c_rpcchan->rc_frm, item);
	return 0;
}

int c2_rpc_reply_post(struct c2_rpc_item	*request,
		      struct c2_rpc_item	*reply)
{
	struct c2_rpc_slot_ref	*sref;
	struct c2_rpc_machine   *machine;
	struct c2_rpc_item	*tmp;
	struct c2_rpc_slot	*slot;

	C2_PRE(request != NULL && reply != NULL);
	C2_PRE(request->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS);
	C2_PRE(request->ri_session != NULL);
	C2_PRE(reply->ri_type != NULL);
	C2_PRE(reply->ri_ops != NULL && reply->ri_ops->rio_free != NULL);
	C2_PRE(c2_rpc_item_size(reply) <=
			c2_rpc_session_get_max_item_size(request->ri_session));
	reply->ri_rpc_time = c2_time_now();
	reply->ri_session  = request->ri_session;

	/* BEWARE: structure instance copy ahead */
	reply->ri_slot_refs[0] = request->ri_slot_refs[0];
	sref = &reply->ri_slot_refs[0];
	/* don't need values of sr_link and sr_ready_link of request item */
	c2_list_link_init(&sref->sr_link);
	c2_list_link_init(&sref->sr_ready_link);

	sref->sr_item = reply;

	reply->ri_prio     = request->ri_prio;
	reply->ri_deadline = 0;
	reply->ri_error    = 0;
	reply->ri_state    = RPC_ITEM_SUBMITTED;

	slot = sref->sr_slot;
	machine = slot->sl_session->s_conn->c_rpc_machine;

	c2_rpc_machine_lock(machine);
	/*
	 * This hold will be released when the item is SENT or FAILED.
	 * See rpc/frmops.c:item_done()
	 */
	c2_rpc_session_hold_busy(reply->ri_session);
	c2_rpc_slot_reply_received(slot, reply, &tmp);
	C2_ASSERT(tmp == request);

	c2_rpc_machine_unlock(machine);
	return 0;
}
C2_EXPORTED(c2_rpc_reply_post);

int c2_rpc_unsolicited_item_post(const struct c2_rpc_conn *conn,
				 struct c2_rpc_item       *item)
{
	C2_PRE(conn != NULL);
	C2_PRE(item != NULL && c2_rpc_item_is_unsolicited(item));

	item->ri_state    = RPC_ITEM_SUBMITTED;
	item->ri_rpc_time = c2_time_now();

	c2_rpc_machine_lock(conn->c_rpc_machine);

	c2_rpc_frm_enq_item(&conn->c_rpcchan->rc_frm, item);

	c2_rpc_machine_unlock(conn->c_rpc_machine);
	return 0;
}

static void rpc_chan_ref_release(struct c2_ref *ref)
{
	struct c2_rpc_chan *chan;

	C2_PRE(ref != NULL);

	chan = container_of(ref, struct c2_rpc_chan, rc_ref);
	C2_ASSERT(chan != NULL);
	C2_ASSERT(c2_rpc_machine_is_locked(chan->rc_rpc_machine));

	c2_list_del(&chan->rc_linkage);
	c2_rpc_frm_fini(&chan->rc_frm);
	c2_free(chan);
}

static int rpc_chan_create(struct c2_rpc_chan **chan,
			   struct c2_rpc_machine *machine,
			   struct c2_net_end_point *dest_ep,
			   uint64_t max_rpcs_in_flight)
{
	struct c2_rpc_frm_constraints  constraints;
	struct c2_net_domain          *ndom;
	struct c2_rpc_chan            *ch;

	C2_PRE(chan != NULL);
	C2_PRE(dest_ep != NULL);

	C2_PRE(c2_rpc_machine_is_locked(machine));

	C2_ALLOC_PTR_ADDB(ch, &machine->rm_addb, &c2_rpc_machine_addb_loc);
	if (ch == NULL) {
		*chan = NULL;
		return -ENOMEM;
	}

	ch->rc_rpc_machine = machine;
	ch->rc_destep = dest_ep;
	c2_ref_init(&ch->rc_ref, 1, rpc_chan_ref_release);
	c2_net_end_point_get(dest_ep);

	ndom = machine->rm_tm.ntm_dom;

	constraints.fc_max_nr_packets_enqed = max_rpcs_in_flight;
	constraints.fc_max_packet_size = machine->rm_min_recv_size;
	constraints.fc_max_nr_bytes_accumulated =
				constraints.fc_max_packet_size;
	constraints.fc_max_nr_segments =
				c2_net_domain_get_max_buffer_segments(ndom);

	c2_rpc_frm_init(&ch->rc_frm, machine, ch,
			constraints, &c2_rpc_frm_default_ops);
	c2_list_add(&machine->rm_chans, &ch->rc_linkage);
	*chan = ch;
	return 0;
}

/* Put buffer back into the pool */
static void rpc_recv_pool_buffer_put(struct c2_net_buffer *nb)
{
	struct c2_net_transfer_mc *tm;
	C2_PRE(nb != NULL);
	tm = nb->nb_tm;

	C2_PRE(tm != NULL);
	C2_PRE(tm->ntm_recv_pool != NULL && nb->nb_pool !=NULL);
	C2_PRE(tm->ntm_recv_pool == nb->nb_pool);

	nb->nb_ep = NULL;
	c2_net_buffer_pool_lock(tm->ntm_recv_pool);
	c2_net_buffer_pool_put(tm->ntm_recv_pool, nb,
			       tm->ntm_pool_colour);
	c2_net_buffer_pool_unlock(tm->ntm_recv_pool);
}

static int rpc_tm_setup(struct c2_rpc_machine *machine,
			struct c2_net_domain *net_dom, const char *ep_addr)
{
	int		rc;
	struct c2_clink tmwait;

	C2_PRE(machine != NULL);
	C2_PRE(net_dom != NULL);
	C2_PRE(ep_addr != NULL);
	C2_PRE(machine->rm_buffer_pool != NULL);

	machine->rm_tm.ntm_state = C2_NET_TM_UNDEFINED;
	machine->rm_tm.ntm_callbacks = &c2_rpc_tm_callbacks;

	/* Initialize the net transfer machine. */
	rc = c2_net_tm_init(&machine->rm_tm, net_dom);
	if (rc < 0)
		return rc;

	rc = c2_net_tm_pool_attach(&machine->rm_tm, machine->rm_buffer_pool,
				   &c2_rpc_rcv_buf_callbacks,
				   machine->rm_min_recv_size,
				   machine->rm_max_recv_msgs,
				   machine->rm_tm_recv_queue_min_length);
	if (rc < 0) {
		c2_net_tm_fini(&machine->rm_tm);
		return rc;
	}

	c2_net_tm_colour_set(&machine->rm_tm, machine->rm_tm_colour);

	/* Start the transfer machine so that users of this rpc_machine
	   can send/receive messages. */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&machine->rm_tm.ntm_chan, &tmwait);

	rc = c2_net_tm_start(&machine->rm_tm, ep_addr);
	if (rc < 0)
		goto cleanup;

	/* Wait on transfer machine channel till transfer machine is
	   actually started. */
	while (machine->rm_tm.ntm_state != C2_NET_TM_STARTED)
		c2_chan_wait(&tmwait);

	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	return rc;
cleanup:
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);
	if (machine->rm_tm.ntm_state <= C2_NET_TM_FAILED)
		c2_net_tm_fini(&machine->rm_tm);
	return rc;
}

static struct c2_rpc_chan *rpc_chan_locate(struct c2_rpc_machine *machine,
					   struct c2_net_end_point *dest_ep)
{
	bool			 found = false;
	struct c2_rpc_chan	*chan;

	C2_PRE(dest_ep != NULL);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	/* Locate the chan from rpc_machine->chans list. */
	c2_list_for_each_entry(&machine->rm_chans, chan, struct c2_rpc_chan,
			       rc_linkage) {
		C2_ASSERT(chan->rc_destep->nep_tm->ntm_dom ==
			  dest_ep->nep_tm->ntm_dom);
		if (chan->rc_destep == dest_ep) {
			found = true;
			break;
		}
	}

	if (found) {
		c2_ref_get(&chan->rc_ref);
		c2_net_end_point_get(chan->rc_destep);
	} else
		chan = NULL;
	return chan;
}

struct c2_rpc_chan *rpc_chan_get(struct c2_rpc_machine *machine,
				 struct c2_net_end_point *dest_ep,
				 uint64_t max_rpcs_in_flight)
{
	struct c2_rpc_chan	*chan;

	C2_PRE(dest_ep != NULL);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	chan = rpc_chan_locate(machine, dest_ep);
	if (chan == NULL)
		rpc_chan_create(&chan, machine, dest_ep, max_rpcs_in_flight);

	return chan;
}

void rpc_chan_put(struct c2_rpc_chan *chan)
{
	struct c2_rpc_machine *machine;

	C2_PRE(chan != NULL);

	machine = chan->rc_rpc_machine;
	C2_PRE(c2_rpc_machine_is_locked(machine));

	c2_net_end_point_put(chan->rc_destep);
	c2_ref_put(&chan->rc_ref);
}

static void rpc_tm_cleanup(struct c2_rpc_machine *machine)
{
	int			   rc;
	struct c2_clink		   tmwait;
	struct c2_net_transfer_mc *tm = &machine->rm_tm;

	C2_PRE(machine != NULL);

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	rc = c2_net_tm_stop(tm, true);

	if (rc < 0) {
		c2_clink_del(&tmwait);
		c2_clink_fini(&tmwait);
		C2_ADDB_ADD(&machine->rm_addb, &c2_rpc_machine_addb_loc,
			    c2_rpc_machine_func_fail, "c2_net_tm_stop", 0);
		return;
	}
	/* Wait for transfer machine to stop. */
	while (tm->ntm_state != C2_NET_TM_STOPPED &&
	       tm->ntm_state != C2_NET_TM_FAILED)
		c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	/* Fini the transfer machine here and deallocate the chan. */
	c2_net_tm_fini(tm);
}

int c2_rpc_reply_timedwait(struct c2_clink *clink, const c2_time_t timeout)
{
	C2_PRE(clink != NULL);
	C2_PRE(c2_clink_is_armed(clink));

	return c2_chan_timedwait(clink, timeout) ? 0 : -ETIMEDOUT;
}
C2_EXPORTED(c2_rpc_reply_timedwait);

int c2_rpc_group_timedwait(struct c2_rpc_group *group, const c2_time_t *timeout)
{
	return 0;
}

/**
   The callback routine to be called once the transfer machine
   receives a buffer. This subroutine later invokes decoding of
   net buffer and then notifies sessions component about every
   incoming rpc item.
 */
static void rpc_net_buf_received(const struct c2_net_buffer_event *ev)
{
	int		       rc;
	c2_time_t	       now;
	struct c2_rpc	       rpc;
	struct c2_rpc_item    *item;
	struct c2_rpc_item    *next_item;
	struct c2_net_buffer  *nb;
	struct c2_rpc_chan    *chan;
	struct c2_rpc_machine *machine;

	C2_PRE(ev != NULL && ev->nbe_buffer != NULL);

	/* Decode the buffer, get an RPC from it, traverse the
	   list of rpc items from that rpc and post reply callbacks
	   for each rpc item. */
	nb	      = ev->nbe_buffer;
	nb->nb_length = ev->nbe_length;
	nb->nb_ep     = ev->nbe_ep;
	machine       = container_of(nb->nb_tm, struct c2_rpc_machine, rm_tm);

	c2_rpc_machine_lock(machine);

	if (ev->nbe_status != 0) {
		if (ev->nbe_status != -ECANCELED)
			C2_ADDB_ADD(&machine->rm_addb,
				    &c2_rpc_machine_addb_loc,
				    c2_rpc_machine_func_fail,
				    "Buffer event reported failure",
				    ev->nbe_status);
		rc = ev->nbe_status;
		goto last;
	}

	chan = rpc_chan_locate(machine, nb->nb_ep);
	if (chan != NULL) {
		rpc_chan_put(chan);
	}
	c2_rpcobj_init(&rpc);
	rc = c2_rpc_decode(&rpc, nb, ev->nbe_length, ev->nbe_offset);
last:
	C2_ASSERT(nb->nb_pool != NULL);
	if (!(nb->nb_flags & C2_NET_BUF_QUEUED))
		rpc_recv_pool_buffer_put(nb);

	if (rc == 0) {
		rpcobj_exit_stats_set(&rpc, machine, C2_RPC_PATH_INCOMING);
		now = c2_time_now();
		c2_list_for_each_entry_safe(&rpc.r_items, item, next_item,
					    struct c2_rpc_item,
					    ri_rpcobject_linkage) {
			c2_list_del(&item->ri_rpcobject_linkage);

			if (c2_rpc_item_is_conn_establish(item))
				c2_rpc_fop_conn_establish_ctx_init(item,
								   ev->nbe_ep,
								   machine);

			item->ri_rpc_time = now;
			rc = c2_rpc_item_received(item, machine);
			/*
			 * If 'item' is conn terminate reply then, do not
			 * access item, after this point. In which case the
			 * item might have already been freed.
			 */
			C2_ASSERT(rc == 0);
		}
	}
	c2_rpc_machine_unlock(machine);
}

static int rpc_net_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nbuf, enum c2_net_queue_type qtype,
		uint64_t rpc_size)
{
	int		      rc;
	int32_t		      segs_nr;
	c2_bcount_t	      seg_size;
	c2_bcount_t	      buf_size;
	c2_bcount_t	      nrsegs;
	struct c2_net_buffer *nb;

	C2_PRE(net_dom != NULL);
	C2_PRE((qtype == C2_NET_QT_MSG_RECV) || (qtype == C2_NET_QT_MSG_SEND));

	/* As of now, we keep net_buffers as inline objects in c2_rpc_frm_buffer
	   for sending part. Hence we need not allocate buffers in such case.
	   This will change once we have a pool of buffers at sending side. */
	if (qtype == C2_NET_QT_MSG_RECV) {
		C2_ALLOC_PTR(nb);
		if (nb == NULL)
			return -ENOMEM;
	} else
		nb = *nbuf;

	buf_size = c2_net_domain_get_max_buffer_size(net_dom);
	segs_nr = c2_net_domain_get_max_buffer_segments(net_dom);
	seg_size = c2_net_domain_get_max_buffer_segment_size(net_dom);
	if (rpc_size != 0)
		buf_size = rpc_size;

	/* Allocate the bufvec of size = min((buf_size), (segs_nr * seg_size)).
	   We keep the segment size constant. So mostly the number of segments
	   is changed here. */
	if (buf_size > (segs_nr * seg_size)) {
		nrsegs = segs_nr;
	} else {
		nrsegs = buf_size / seg_size;
		if (buf_size % seg_size != 0)
			++nrsegs;
	}
	if (nrsegs == 0)
		++nrsegs;

	rc = c2_bufvec_alloc_aligned(&nb->nb_buffer, nrsegs, seg_size,
				     C2_SEG_SHIFT);
	if (rc < 0) {
		if (qtype == C2_NET_QT_MSG_RECV) {
			c2_free(nb);
			*nbuf = NULL;
		}
		return rc;
	}

	nb->nb_flags = 0;
	nb->nb_qtype = qtype;
	if (qtype == C2_NET_QT_MSG_RECV) {
		nb->nb_callbacks	= &c2_rpc_rcv_buf_callbacks;
		nb->nb_min_receive_size = nrsegs * seg_size;
		nb->nb_max_receive_msgs = 1;
	} else
		nb->nb_callbacks = &c2_rpc_send_buf_callbacks;

	/* Register the buffer with given net domain. */
	rc = c2_net_buffer_register(nb, net_dom);
	if (rc < 0) {
		c2_bufvec_free_aligned(&nb->nb_buffer, C2_SEG_SHIFT);
		if (qtype == C2_NET_QT_MSG_RECV) {
			c2_free(nb);
			nb = NULL;
		}
	}
	*nbuf = nb;
	return rc;
}

int send_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nb, uint64_t rpc_size)
{
	C2_PRE(net_dom != NULL);
	C2_PRE(nb != NULL);

	return rpc_net_buffer_allocate(net_dom, nb, C2_NET_QT_MSG_SEND,
			rpc_size);
}

void send_buffer_deallocate(struct c2_net_buffer *nb,
		struct c2_net_domain *net_dom)
{
	C2_PRE(nb != NULL);
	C2_PRE(net_dom != NULL);
	C2_PRE((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);

	c2_net_buffer_deregister(nb, net_dom);
	c2_bufvec_free_aligned(&nb->nb_buffer, C2_SEG_SHIFT);
}

static void __rpc_machine_fini(struct c2_rpc_machine *machine)
{
	c2_list_fini(&machine->rm_chans);
	c2_list_fini(&machine->rm_incoming_conns);
	c2_list_fini(&machine->rm_outgoing_conns);
	c2_list_fini(&machine->rm_ready_slots);
	c2_rpc_services_tlist_fini(&machine->rm_services);

	c2_mutex_fini(&machine->rm_mutex);

	c2_addb_ctx_fini(&machine->rm_addb);
#ifndef __KERNEL__
	c2_rpc_machine_bob_fini(machine);
#endif
}

int c2_rpc_machine_init(struct c2_rpc_machine     *machine,
			struct c2_cob_domain      *dom,
			struct c2_net_domain      *net_dom,
			const char                *ep_addr,
			struct c2_reqh            *reqh,
			struct c2_net_buffer_pool *receive_pool,
			uint32_t		   colour,
			c2_bcount_t		   msg_size,
			uint32_t		   queue_len)
{
	int		rc;
#ifndef __KERNEL__
	struct c2_db_tx tx;
#endif

	C2_PRE(dom	    != NULL);
	C2_PRE(machine	    != NULL);
	C2_PRE(ep_addr	    != NULL);
	C2_PRE(net_dom	    != NULL);
	C2_PRE(receive_pool != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	C2_SET0(machine);
	machine->rm_dom		  = dom;
	machine->rm_reqh	  = reqh;
	machine->rm_buffer_pool	  = receive_pool;
	machine->rm_min_recv_size = c2_rpc_max_msg_size(net_dom, msg_size);
	machine->rm_max_recv_msgs = c2_rpc_max_recv_msgs(net_dom, msg_size);
	machine->rm_tm_colour	  = colour;

	machine->rm_tm_recv_queue_min_length = queue_len;

	c2_list_init(&machine->rm_chans);
	c2_list_init(&machine->rm_incoming_conns);
	c2_list_init(&machine->rm_outgoing_conns);
	c2_list_init(&machine->rm_ready_slots);
	c2_rpc_services_tlist_init(&machine->rm_services);
#ifndef __KERNEL__
	c2_rpc_machine_bob_init(machine);
#endif
	c2_mutex_init(&machine->rm_mutex);

	c2_addb_ctx_init(&machine->rm_addb, &rpc_machine_addb_ctx_type,
			 &c2_addb_global_ctx);

	machine->rm_stopping = false;
	rc = C2_THREAD_INIT(&machine->rm_frm_worker, struct c2_rpc_machine *,
			    NULL, &frm_worker_fn, machine, "frm_worker");
	if (rc != 0)
		goto out_fini;

	rc = rpc_tm_setup(machine, net_dom, ep_addr);
	if (rc != 0)
		goto out_stop_frm_worker;

#ifndef __KERNEL__
	rc = c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	if (rc == 0) {
		rc = c2_rpc_root_session_cob_create(dom, &tx);
		if (rc == 0) {
			c2_db_tx_commit(&tx);
		} else {
			c2_db_tx_abort(&tx);
			rpc_tm_cleanup(machine);
			goto out_stop_frm_worker;
		}
	}
#endif
	return rc;

out_stop_frm_worker:
	machine->rm_stopping = true;
	c2_thread_join(&machine->rm_frm_worker);

out_fini:
	__rpc_machine_fini(machine);
	return rc;
}
C2_EXPORTED(c2_rpc_machine_init);

/**
   Worker thread that runs formation periodically on all formation machines,
   in an attempt to send timedout items.

   XXX This entire routine is temporary. The item deadline timeout mechanism
       should be based on generic sm framework.
 */
static void frm_worker_fn(struct c2_rpc_machine *machine)
{
	struct c2_rpc_chan *chan;
	enum { MILLI_SEC = 1000 * 1000 };

	C2_PRE(machine != NULL);

	while (true) {
		c2_rpc_machine_lock(machine);
		if (machine->rm_stopping) {
			c2_rpc_machine_unlock(machine);
			return;
		}
		c2_list_for_each_entry(&machine->rm_chans, chan,
				       struct c2_rpc_chan, rc_linkage) {
			c2_rpc_frm_run_formation(&chan->rc_frm);
		}
		c2_rpc_machine_unlock(machine);
		c2_nanosleep(c2_time(0, 100 * MILLI_SEC), NULL);
	}
}

/**
   XXX Temporary. This routine will be discarded, once rpc-core starts
   providing c2_rpc_item::ri_ops::rio_sent() callback.

   In-memory state of conn should be cleaned up when reply to CONN_TERMINATE
   has been sent. As of now, rpc-core does not provide this callback. So this
   is a temporary routine, that cleans up all terminated connections from
   rpc connection list maintained in rpc_machine.
 */
static void conn_list_fini(struct c2_list *list)
{
        struct c2_rpc_conn *conn;
        struct c2_rpc_conn *conn_next;

        C2_PRE(list != NULL);

        c2_list_for_each_entry_safe(list, conn, conn_next, struct c2_rpc_conn,
                        c_link) {

                c2_rpc_conn_terminate_reply_sent(conn);
        }
}

void c2_rpc_machine_fini(struct c2_rpc_machine *machine)
{
	C2_PRE(machine != NULL);

	c2_rpc_machine_lock(machine);
	machine->rm_stopping = true;
	c2_rpc_machine_unlock(machine);

	c2_thread_join(&machine->rm_frm_worker);

	c2_rpc_machine_lock(machine);

	C2_PRE(c2_list_is_empty(&machine->rm_outgoing_conns));
	conn_list_fini(&machine->rm_incoming_conns);

	c2_rpc_machine_unlock(machine);

	rpc_tm_cleanup(machine);

	__rpc_machine_fini(machine);
}
C2_EXPORTED(c2_rpc_machine_fini);

void c2_rpc_machine_lock(struct c2_rpc_machine *machine)
{
	C2_ENTRY("machine %p", machine);

	C2_PRE(machine != NULL);
	c2_mutex_lock(&machine->rm_mutex);

	C2_LEAVE();
}

void c2_rpc_machine_unlock(struct c2_rpc_machine *machine)
{
	C2_ENTRY("machine %p", machine);

	C2_PRE(machine != NULL);
	c2_mutex_unlock(&machine->rm_mutex);

	C2_LEAVE();
}

bool c2_rpc_machine_is_locked(const struct c2_rpc_machine *machine)
{
	C2_PRE(machine != NULL);
	return c2_mutex_is_locked(&machine->rm_mutex);
}

/**
  Set the stats for outgoing rpc object
  @param rpcobj - incoming or outgoing rpc object
  @param mach - rpc_machine for which the rpc object belongs to
  @param path - enum distinguishing whether the item is incoming or outgoing
 */
void rpcobj_exit_stats_set(const struct c2_rpc *rpcobj,
		struct c2_rpc_machine *mach, const enum c2_rpc_item_path path)
{
	C2_PRE(rpcobj != NULL);
	C2_PRE(c2_rpc_machine_is_locked(mach));

	mach->rm_rpc_stats[path].rs_rpcs_nr++;
}

/**
  Set the stats for outgoing rpc item
  @param item - incoming or outgoing rpc item
  @param path - enum distinguishing whether the item is incoming or outgoing
 */
void item_exit_stats_set(struct c2_rpc_item *item,
			 enum c2_rpc_item_path path)
{
	struct c2_rpc_machine *machine;
	struct c2_rpc_stats   *st;

	C2_PRE(item != NULL && item->ri_session != NULL);

	machine = item->ri_session->s_conn->c_rpc_machine;
	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	C2_PRE(IS_IN_ARRAY(path, machine->rm_rpc_stats));

	item->ri_rpc_time = c2_time_sub(c2_time_now(), item->ri_rpc_time);

	st = &machine->rm_rpc_stats[path];
        st->rs_cumu_lat += item->ri_rpc_time;
	st->rs_min_lat = st->rs_min_lat ? : item->ri_rpc_time;
	st->rs_min_lat = min64u(st->rs_min_lat, item->ri_rpc_time);
	st->rs_max_lat = st->rs_max_lat ? : item->ri_rpc_time;
	st->rs_max_lat = max64u(st->rs_max_lat, item->ri_rpc_time);

        st->rs_items_nr++;
        st->rs_bytes_nr += c2_fop_item_type_default_onwire_size(item);
}

size_t c2_rpc_bytes_per_sec(struct c2_rpc_machine *machine,
			    const enum c2_rpc_item_path path)
{
	struct c2_rpc_stats *stats;

	C2_PRE(machine != NULL);
	C2_PRE(IS_IN_ARRAY(path, machine->rm_rpc_stats));

	stats = &machine->rm_rpc_stats[path];
	return stats->rs_bytes_nr / stats->rs_cumu_lat;
}

c2_time_t c2_rpc_avg_item_time(struct c2_rpc_machine *machine,
			       const enum c2_rpc_item_path path)
{
	struct c2_rpc_stats *stats;

	C2_PRE(machine != NULL);
	C2_PRE(IS_IN_ARRAY(path, machine->rm_rpc_stats));

	stats = &machine->rm_rpc_stats[path];
	return stats->rs_cumu_lat / stats->rs_items_nr;
}

static void buffer_pool_low(struct c2_net_buffer_pool *bp)
{
	/* Buffer pool is below threshold.  */
}

static const struct c2_net_buffer_pool_ops b_ops = {
	.nbpo_not_empty	      = c2_net_domain_buffer_pool_not_empty,
	.nbpo_below_threshold = buffer_pool_low,
};

int c2_rpc_net_buffer_pool_setup(struct c2_net_domain *ndom,
				 struct c2_net_buffer_pool *app_pool,
				 uint32_t bufs_nr, uint32_t tm_nr)
{
	int	    rc;
	uint32_t    segs_nr;
	c2_bcount_t seg_size;

	C2_PRE(ndom != NULL);
	C2_PRE(app_pool != NULL);
	C2_PRE(bufs_nr != 0);

	seg_size = c2_rpc_max_seg_size(ndom);
	segs_nr  = c2_rpc_max_segs_nr(ndom);
	app_pool->nbp_ops = &b_ops;
	rc = c2_net_buffer_pool_init(app_pool, ndom,
				     C2_NET_BUFFER_POOL_THRESHOLD,
				     segs_nr, seg_size, tm_nr, C2_SEG_SHIFT);
	if (rc != 0)
		return rc;
	c2_net_buffer_pool_lock(app_pool);
	rc = c2_net_buffer_pool_provision(app_pool, bufs_nr);
	c2_net_buffer_pool_unlock(app_pool);
	return rc != bufs_nr ? -ENOMEM : 0;
}
C2_EXPORTED(c2_rpc_net_buffer_pool_setup);

void c2_rpc_net_buffer_pool_cleanup(struct c2_net_buffer_pool *app_pool)
{
	C2_PRE(app_pool != NULL);

	c2_net_buffer_pool_fini(app_pool);
}
C2_EXPORTED(c2_rpc_net_buffer_pool_cleanup);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
