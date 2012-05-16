/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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

#include "cob/cob.h"
#include "rpc/rpc2.h"
#include "rpc/rpcdbg.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/types.h"
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

/* Forward declarations. */
static int recv_buffer_allocate_nr(struct c2_net_domain  *net_dom,
				   struct c2_rpc_machine *machine);
static void recv_buffer_deallocate_nr(struct c2_rpc_machine *machine,
				      bool tm_active, uint32_t nr);
static void rpc_net_buf_received(const struct c2_net_buffer_event *ev);
static void rpc_tm_cleanup(struct c2_rpc_machine *machine);


extern void frm_rpcs_inflight_dec(struct c2_rpc_frm_sm *frm_sm);
extern void frm_sm_init(struct c2_rpc_frm_sm *frm_sm,
			uint64_t max_rpcs_in_flight);
extern void frm_sm_fini(struct c2_rpc_frm_sm *frm_sm);
extern int frm_ubitem_added(struct c2_rpc_item *item);
extern void frm_net_buffer_sent(const struct c2_net_buffer_event *ev);
extern void rpcobj_exit_stats_set(const struct c2_rpc *rpcobj,
		struct c2_rpc_machine *mach, enum c2_rpc_item_path path);

int c2_rpc__post_locked(struct c2_rpc_item *item);

C2_TL_DESCR_DEFINE(rpcitem, "rpc item tlist", , struct c2_rpc_item, ri_field,
	           ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
		   C2_RPC_ITEM_HEAD_MAGIC);

C2_TL_DEFINE(rpcitem, , struct c2_rpc_item);

/* Number of default receive c2_net_buffers to be used with
   each transfer machine.*/
enum {
	C2_RPC_TM_RECV_BUFFERS_NR = 64,
};

/* ADDB Instrumentation for rpccore. */
static const struct c2_addb_ctx_type rpc_machine_addb_ctx_type = {
	        .act_name = "rpc-machine"
};

static const struct c2_addb_loc rpc_machine_addb_loc = {
	        .al_name = "rpc-machine"
};

C2_ADDB_EV_DEFINE(rpc_machine_func_fail, "rpc_machine_func_fail",
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

void c2_rpc_item_init(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	*sref;

	C2_SET0(item);

	item->ri_state      = RPC_ITEM_UNINITIALIZED;
	item->ri_head_magic = C2_RPC_ITEM_HEAD_MAGIC;
	item->ri_link_magic = C2_RPC_ITEM_FIELD_MAGIC;

	sref = &item->ri_slot_refs[0];

	sref->sr_slot_id    = SLOT_ID_INVALID;
	sref->sr_sender_id  = SENDER_ID_INVALID;
	sref->sr_session_id = SESSION_ID_INVALID;

	c2_list_link_init(&sref->sr_link);
	c2_list_link_init(&sref->sr_ready_link);

        c2_list_link_init(&item->ri_unbound_link);

        c2_list_link_init(&item->ri_rpcobject_linkage);
	c2_list_link_init(&item->ri_unformed_linkage);
        c2_list_link_init(&item->ri_group_linkage);
        rpcitem_tlink_init(item);
	rpcitem_tlist_init(&item->ri_compound_items);

	c2_chan_init(&item->ri_chan);
}
C2_EXPORTED(c2_rpc_item_init);

void c2_rpc_item_fini(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	*sref;

	c2_chan_fini(&item->ri_chan);

	sref = &item->ri_slot_refs[0];
	sref->sr_slot_id = SLOT_ID_INVALID;
	c2_list_link_fini(&sref->sr_link);
	c2_list_link_fini(&sref->sr_ready_link);

	sref->sr_sender_id = SENDER_ID_INVALID;
	sref->sr_session_id = SESSION_ID_INVALID;

        c2_list_link_fini(&item->ri_unbound_link);

        c2_list_link_fini(&item->ri_rpcobject_linkage);
	c2_list_link_fini(&item->ri_unformed_linkage);
        c2_list_link_fini(&item->ri_group_linkage);
	rpcitem_tlink_fini(item);
	rpcitem_tlist_fini(&item->ri_compound_items);
	item->ri_state = RPC_ITEM_FINALIZED;
}
C2_EXPORTED(c2_rpc_item_fini);

int c2_rpc_post(struct c2_rpc_item *item)
{
	struct c2_rpc_machine *machine;
	int                    rc;

	C2_PRE(item->ri_session != NULL);

	machine = item->ri_session->s_conn->c_rpc_machine;

	c2_rpc_machine_lock(machine);
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

	C2_ASSERT(c2_rpc_machine_is_locked(session->s_conn->c_rpc_machine));

	item->ri_rpc_time = c2_time_now();

	item->ri_state = RPC_ITEM_SUBMITTED;
	frm_ubitem_added(item);

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
	reply->ri_deadline = request->ri_deadline;
	reply->ri_error    = 0;
	reply->ri_state    = RPC_ITEM_SUBMITTED;

	slot = sref->sr_slot;
	machine = slot->sl_session->s_conn->c_rpc_machine;

	c2_rpc_machine_lock(machine);

	c2_rpc_slot_reply_received(slot, reply, &tmp);
	C2_ASSERT(tmp == request);

	c2_rpc_machine_unlock(machine);
	return 0;
}
C2_EXPORTED(c2_rpc_reply_post);

bool c2_rpc_item_is_update(const struct c2_rpc_item *item)
{
	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_MUTABO) != 0;
}

bool c2_rpc_item_is_request(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_REQUEST) != 0;
}

bool c2_rpc_item_is_reply(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_REPLY) != 0;
}

bool c2_rpc_item_is_unsolicited(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);
	C2_PRE(item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_UNSOLICITED) != 0;
}

bool c2_rpc_item_is_bound(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);

	return item->ri_slot_refs[0].sr_slot != NULL;
}

bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item)
{
	return !c2_rpc_item_is_bound(item) && !c2_rpc_item_is_unsolicited(item);
}

int c2_rpc_unsolicited_item_post(const struct c2_rpc_conn *conn,
		struct c2_rpc_item *item)
{
	struct c2_rpc_session	*session_zero;

	C2_PRE(conn != NULL);
	C2_PRE(item != NULL);

	session_zero = c2_rpc_conn_session0(conn);

	item->ri_session = session_zero;
	item->ri_state = RPC_ITEM_SUBMITTED;

	item->ri_rpc_time = c2_time_now();

	c2_rpc_machine_lock(conn->c_rpc_machine);

	frm_ubitem_added(item);

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

	/* Destroy the chan structure. */
	c2_list_del(&chan->rc_linkage);
	frm_sm_fini(&chan->rc_frmsm);
	c2_free(chan);
}

static int rpc_chan_create(struct c2_rpc_chan **chan,
			   struct c2_rpc_machine *machine,
			   struct c2_net_end_point *dest_ep,
			   uint64_t max_rpcs_in_flight)
{
	struct c2_rpc_chan *ch;

	C2_PRE(chan != NULL);
	C2_PRE(dest_ep != NULL);

	C2_PRE(c2_rpc_machine_is_locked(machine));

	C2_ALLOC_PTR_ADDB(ch, &machine->rm_rpc_machine_addb,
			       &rpc_machine_addb_loc);
	if (ch == NULL) {
		*chan = NULL;
		return -ENOMEM;
	}

	ch->rc_rpc_machine = machine;
	ch->rc_destep = dest_ep;
	c2_ref_init(&ch->rc_ref, 1, rpc_chan_ref_release);
	c2_net_end_point_get(dest_ep);
	frm_sm_init(&ch->rc_frmsm, max_rpcs_in_flight);
	c2_list_add(&machine->rm_chans, &ch->rc_linkage);
	*chan = ch;
	return 0;
}

static int rpc_tm_setup(struct c2_rpc_machine *machine,
			struct c2_net_domain *net_dom, const char *ep_addr)
{
	int		rc;
	struct c2_clink tmwait;

	C2_PRE(machine != NULL);
	C2_PRE(net_dom != NULL);
	C2_PRE(ep_addr != NULL);

	/* Allocate space for pointers of recv net buffers. */
	C2_ALLOC_ARR(machine->rm_rcv_buffers, C2_RPC_TM_RECV_BUFFERS_NR);
	if (machine->rm_rcv_buffers == NULL) {
		C2_ADDB_ADD(&machine->rm_rpc_machine_addb,
				&rpc_machine_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}

	machine->rm_tm.ntm_state = C2_NET_TM_UNDEFINED;
	machine->rm_tm.ntm_callbacks = &c2_rpc_tm_callbacks;

	/* Initialize the net transfer machine. */
	rc = c2_net_tm_init(&machine->rm_tm, net_dom);
	if (rc < 0) {
		c2_free(machine->rm_rcv_buffers);
		return rc;
	}

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

	/* Add buffers for receiving messages to this transfer machine. */
	rc = recv_buffer_allocate_nr(net_dom, machine);
	if (rc < 0) {
		rpc_tm_cleanup(machine);
		return rc;
	}

	return rc;
cleanup:
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);
	c2_free(machine->rm_rcv_buffers);
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
	int		cnt;
	int		rc;
	struct c2_clink	tmwait;

	C2_PRE(machine != NULL);

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&machine->rm_tm.ntm_chan, &tmwait);

	rc = c2_net_tm_stop(&machine->rm_tm, false);
	if (rc < 0) {
		c2_clink_del(&tmwait);
		c2_clink_fini(&tmwait);
		C2_ADDB_ADD(&machine->rm_rpc_machine_addb,
			    &rpc_machine_addb_loc, rpc_machine_func_fail,
			    "c2_net_tm_stop", 0);
		return;
	}

	/* Wait for transfer machine to stop. */
	while (machine->rm_tm.ntm_state != C2_NET_TM_STOPPED)
		c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	/* Delete all the buffers from net domain. */
	recv_buffer_deallocate_nr(machine, false, C2_RPC_TM_RECV_BUFFERS_NR);

	/* Fini the transfer machine here and deallocate the chan. */
	c2_net_tm_fini(&machine->rm_tm);
	for (cnt = 0; cnt < C2_RPC_TM_RECV_BUFFERS_NR; ++cnt)
		C2_ASSERT(machine->rm_rcv_buffers[cnt] == NULL);

	c2_free(machine->rm_rcv_buffers);
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
	int			 rc;
	c2_time_t		 now;
	struct c2_rpc		 rpc;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*next_item;
	struct c2_net_buffer	*nb;
	struct c2_rpc_chan	*chan;
	struct c2_rpc_machine	*machine;

	C2_PRE(ev != NULL && ev->nbe_buffer != NULL);

	/* Decode the buffer, get an RPC from it, traverse the
	   list of rpc items from that rpc and post reply callbacks
	   for each rpc item. */
	nb = ev->nbe_buffer;

	machine = container_of(nb->nb_tm, struct c2_rpc_machine, rm_tm);

	c2_rpc_machine_lock(machine);

	if (ev->nbe_status != 0) {
		C2_ADDB_ADD(&machine->rm_rpc_machine_addb,
			    &rpc_machine_addb_loc,
			    rpc_machine_func_fail,
			    "Buffer event reported failure %d",
			    ev->nbe_status);
		goto last;
	}
	nb->nb_length = ev->nbe_length;
	nb->nb_ep = ev->nbe_ep;

	chan = rpc_chan_locate(machine, nb->nb_ep);
	if (chan != NULL) {
		frm_rpcs_inflight_dec(&chan->rc_frmsm);
		rpc_chan_put(chan);
	}

	c2_rpcobj_init(&rpc);
	rc = c2_rpc_decode(&rpc, nb);
	if (rc < 0)
		goto last;

	rpcobj_exit_stats_set(&rpc, machine, C2_RPC_PATH_INCOMING);
	now = c2_time_now();
	c2_list_for_each_entry_safe(&rpc.r_items, item, next_item,
				    struct c2_rpc_item, ri_rpcobject_linkage) {

		c2_list_del(&item->ri_rpcobject_linkage);

		if (c2_rpc_item_is_conn_establish(item))
			c2_rpc_fop_conn_establish_ctx_init(item, nb->nb_ep,
							   machine);

		item->ri_rpc_time = now;
		/*
		 * IMPORTANT:
		 * if type of item is NOT one of item types provided by session
		 * module, then c2_rpc_item_received() => rpc_item_replied()
		 * drops and reaquires machine->rm_mutex
		 */
		rc = c2_rpc_item_received(item, machine);
	}

	/* Add the c2_net_buffer back to the queue of
	   transfer machine. */
last:
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_ep = NULL;
	nb->nb_callbacks = &c2_rpc_rcv_buf_callbacks;
	if (nb->nb_tm->ntm_state == C2_NET_TM_STARTED)
		rc = c2_net_buffer_add(nb, nb->nb_tm);

	c2_rpc_machine_unlock(machine);
}

static int rpc_net_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nbuf, enum c2_net_queue_type qtype,
		uint64_t rpc_size)
{
	int			 rc;
	int32_t			 segs_nr;
	c2_bcount_t		 seg_size;
	c2_bcount_t		 buf_size;
	c2_bcount_t		 nrsegs;
	struct c2_net_buffer	*nb;

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
	if (buf_size > (segs_nr * seg_size))
		nrsegs = segs_nr;
	else
		nrsegs = buf_size / seg_size;
	if (nrsegs == 0)
		++nrsegs;

	rc = c2_bufvec_alloc(&nb->nb_buffer, nrsegs, seg_size);
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
		nb->nb_callbacks = &c2_rpc_rcv_buf_callbacks;
		nb->nb_min_receive_size = nrsegs * seg_size;
		nb->nb_max_receive_msgs = 1;
	} else
		nb->nb_callbacks = &c2_rpc_send_buf_callbacks;

	/* Register the buffer with given net domain. */
	rc = c2_net_buffer_register(nb, net_dom);
	if (rc < 0) {
		c2_bufvec_free(&nb->nb_buffer);
		if (qtype == C2_NET_QT_MSG_RECV) {
			c2_free(nb);
			nb = NULL;
		}
	}
	*nbuf = nb;
	return rc;
}

static int recv_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nb)
{
	C2_PRE(net_dom != NULL);
	C2_PRE(nb != NULL);

	return rpc_net_buffer_allocate(net_dom, nb, C2_NET_QT_MSG_RECV, 0);
}

int send_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nb, uint64_t rpc_size)
{
	C2_PRE(net_dom != NULL);
	C2_PRE(nb != NULL);

	return rpc_net_buffer_allocate(net_dom, nb, C2_NET_QT_MSG_SEND,
			rpc_size);
}

static int recv_buffer_allocate_nr(struct c2_net_domain  *net_dom,
				   struct c2_rpc_machine *machine)
{
	int			 rc;
	uint32_t		 cnt;
	struct c2_net_buffer	*nb;

	C2_PRE(net_dom != NULL);
	C2_PRE(machine != NULL);

	for (cnt = 0; cnt < C2_RPC_TM_RECV_BUFFERS_NR; ++cnt) {
		rc = recv_buffer_allocate(net_dom, &nb);
		if (rc != 0)
			break;
		machine->rm_rcv_buffers[cnt] = nb;
		rc = c2_net_buffer_add(nb, &machine->rm_tm);
		if (rc < 0)
			break;
	}
	if (rc < 0)
		recv_buffer_deallocate_nr(machine, true, cnt);

	return rc;
}

void recv_buffer_deallocate(struct c2_net_buffer *nb,
			    struct c2_rpc_machine *machine, bool tm_active)
{
	struct c2_clink			 tmwait;
	struct c2_net_transfer_mc	*tm;
	struct c2_net_domain		*net_dom;

	C2_PRE(nb != NULL);
	C2_PRE(machine != NULL);

	tm = &machine->rm_tm;
	net_dom = tm->ntm_dom;

	/* Add to a clink to transfer machine's channel to wait for
	   deletion of buffers from transfer machine. */
	if (tm_active) {
		c2_clink_init(&tmwait, NULL);
		c2_clink_add(&tm->ntm_chan, &tmwait);

		c2_net_buffer_del(nb, tm);
		while ((nb->nb_flags & C2_NET_BUF_QUEUED) != 0)
			c2_chan_wait(&tmwait);
		c2_clink_del(&tmwait);
		c2_clink_fini(&tmwait);
	}

	c2_net_buffer_deregister(nb, net_dom);
	c2_bufvec_free(&nb->nb_buffer);
	c2_free(nb);
}

void send_buffer_deallocate(struct c2_net_buffer *nb,
		struct c2_net_domain *net_dom)
{
	C2_PRE(nb != NULL);
	C2_PRE(net_dom != NULL);
	C2_PRE((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);

	c2_net_buffer_deregister(nb, net_dom);
	c2_bufvec_free(&nb->nb_buffer);
}

static void recv_buffer_deallocate_nr(struct c2_rpc_machine *machine,
				      bool tm_active, uint32_t nr)
{
	int			 cnt;
	struct c2_net_buffer	*nb;

	C2_PRE(machine != NULL);
	C2_PRE(nr <= C2_RPC_TM_RECV_BUFFERS_NR);

	for (cnt = 0; cnt < nr; ++cnt) {
		nb = machine->rm_rcv_buffers[cnt];
		recv_buffer_deallocate(nb, machine, tm_active);
		machine->rm_rcv_buffers[cnt] = NULL;
	}
}

static void __rpc_machine_fini(struct c2_rpc_machine *machine)
{
	c2_list_fini(&machine->rm_chans);
	c2_list_fini(&machine->rm_incoming_conns);
	c2_list_fini(&machine->rm_outgoing_conns);
	c2_list_fini(&machine->rm_ready_slots);
	c2_rpc_services_tlist_fini(&machine->rm_services);

	c2_mutex_fini(&machine->rm_mutex);

	c2_addb_ctx_fini(&machine->rm_rpc_machine_addb);
}

int c2_rpc_machine_init(struct c2_rpc_machine *machine,
			struct c2_cob_domain  *dom,
			struct c2_net_domain  *net_dom,
			const char            *ep_addr,
			struct c2_reqh        *reqh)
{
	int		rc;
	struct c2_db_tx tx;

	C2_PRE(dom != NULL);
	C2_PRE(machine != NULL);
	C2_PRE(ep_addr != NULL);
	C2_PRE(net_dom != NULL);

	machine->rm_dom  = dom;
	machine->rm_reqh = reqh;

	C2_SET_ARR0(machine->rm_rpc_stats);

	c2_list_init(&machine->rm_chans);
	c2_list_init(&machine->rm_incoming_conns);
	c2_list_init(&machine->rm_outgoing_conns);
	c2_list_init(&machine->rm_ready_slots);
	c2_rpc_services_tlist_init(&machine->rm_services);

	c2_mutex_init(&machine->rm_mutex);

	c2_addb_ctx_init(&machine->rm_rpc_machine_addb,
			&rpc_machine_addb_ctx_type, &c2_addb_global_ctx);

	c2_db_tx_init(&tx, dom->cd_dbenv, 0);
#ifndef __KERNEL__
	rc = c2_rpc_root_session_cob_create(dom, &tx);
	if (rc != 0)
		goto cleanup;
#endif

	rc = rpc_tm_setup(machine, net_dom, ep_addr);
	if (rc < 0)
		goto cleanup;

	c2_db_tx_commit(&tx);
	return rc;

cleanup:
	c2_db_tx_abort(&tx);
	__rpc_machine_fini(machine);
	return rc;
}
C2_EXPORTED(c2_rpc_machine_init);

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

C2_TL_DESCR_DEFINE(rpcbulk, "rpc bulk buffer list", ,
		   struct c2_rpc_bulk_buf, bb_link, bb_magic,
		   C2_RPC_BULK_BUF_MAGIC, C2_RPC_BULK_MAGIC);

C2_EXPORTED(rpcbulk_tl);

C2_TL_DEFINE(rpcbulk, , struct c2_rpc_bulk_buf);

static bool rpc_bulk_invariant(const struct c2_rpc_bulk *rbulk)
{
	struct c2_rpc_bulk_buf *buf;

	C2_PRE(c2_mutex_is_locked(&rbulk->rb_mutex));
	if (rbulk == NULL || rbulk->rb_magic != C2_RPC_BULK_MAGIC)
		return false;

	c2_tl_for(rpcbulk, &rbulk->rb_buflist, buf) {
		if (buf->bb_rbulk != rbulk)
			return false;
	} c2_tl_endfor;

	return true;
}

static bool rpc_bulk_buf_invariant(const struct c2_rpc_bulk_buf *rbuf)
{
	if (rbuf == NULL ||
	    rbuf->bb_magic != C2_RPC_BULK_BUF_MAGIC ||
	    rbuf->bb_rbulk == NULL ||
	    !rpcbulk_tlink_is_in(rbuf))
		return false;

	return true;
}

static void rpc_bulk_buf_fini(struct c2_rpc_bulk_buf *rbuf)
{
	C2_PRE(rbuf != NULL);

	c2_net_desc_free(&rbuf->bb_nbuf->nb_desc);
	c2_0vec_fini(&rbuf->bb_zerovec);
	if (rbuf->bb_flags & C2_RPC_BULK_NETBUF_ALLOCATED)
		c2_free(rbuf->bb_nbuf);
	c2_free(rbuf);
}

static int rpc_bulk_buf_init(struct c2_rpc_bulk_buf *rbuf, uint32_t segs_nr,
			     struct c2_net_buffer *nb)
{
	int		rc;
	uint32_t	i;
	struct c2_buf	cbuf;
	c2_bindex_t	index = 0;

	C2_PRE(rbuf != NULL);
	C2_PRE(segs_nr > 0);

	rc = c2_0vec_init(&rbuf->bb_zerovec, segs_nr);
	if (rc != 0)
		return rc;

	rbuf->bb_flags = 0;
	if (nb == NULL) {
		C2_ALLOC_PTR(rbuf->bb_nbuf);
		if (rbuf->bb_nbuf == NULL) {
			c2_0vec_fini(&rbuf->bb_zerovec);
			return -ENOMEM;
		}
		rbuf->bb_flags |= C2_RPC_BULK_NETBUF_ALLOCATED;
		rbuf->bb_nbuf->nb_buffer = rbuf->bb_zerovec.z_bvec;
	} else {
		rbuf->bb_nbuf = nb;
		/*
		 * Incoming buffer can be bigger while the bulk transfer
		 * request could refer to smaller size. Hence initialize
		 * the zero vector to get correct size of bulk transfer.
		 */
		for (i = 0; i < segs_nr; ++i) {
			cbuf.b_addr = nb->nb_buffer.ov_buf[i];
			cbuf.b_nob = nb->nb_buffer.ov_vec.v_count[i];
			rc = c2_0vec_cbuf_add(&rbuf->bb_zerovec, &cbuf, &index);
			if (rc != 0) {
				c2_0vec_fini(&rbuf->bb_zerovec);
				return rc;
			}
		}
	}

	rpcbulk_tlink_init(rbuf);
	rbuf->bb_magic = C2_RPC_BULK_BUF_MAGIC;
	return rc;
}

static void rpc_bulk_buf_cb(const struct c2_net_buffer_event *evt)
{
	struct c2_rpc_bulk	*rbulk;
	struct c2_rpc_bulk_buf	*buf;
	struct c2_net_buffer	*nb;
	bool			 receiver = false;

	C2_PRE(evt != NULL);
	C2_PRE(evt->nbe_buffer != NULL);

	nb = evt->nbe_buffer;
	buf = (struct c2_rpc_bulk_buf *)nb->nb_app_private;
	rbulk = buf->bb_rbulk;

	C2_ASSERT(rpc_bulk_buf_invariant(buf));
	C2_ASSERT(rpcbulk_tlink_is_in(buf));

	if (nb->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV ||
	    nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV)
		nb->nb_length = evt->nbe_length;

	if (nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV ||
	    nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND)
		receiver = true;

	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(rpc_bulk_invariant(rbulk));
	/*
	 * Change the status code of struct c2_rpc_bulk only if it is
	 * zero so far. This will ensure that return code of first failure
	 * from list of net buffers in struct c2_rpc_bulk will be maintained.
	 * Buffers are canceled by io coalescing code which in turn sends
	 * a coalesced buffer and cancels member buffers. Hence -ECANCELED
	 * is not treated as an error here.
	 */
	if (rbulk->rb_rc == 0 && evt->nbe_status != -ECANCELED)
		rbulk->rb_rc = evt->nbe_status;

	rpcbulk_tlist_del(buf);
	if (receiver) {
		C2_ASSERT(c2_chan_has_waiters(&rbulk->rb_chan));
		if (rpcbulk_tlist_is_empty(&rbulk->rb_buflist))
			c2_chan_signal(&rbulk->rb_chan);
	}
	if (buf->bb_flags & C2_RPC_BULK_NETBUF_REGISTERED)
		c2_net_buffer_deregister(nb, nb->nb_dom);

	rpc_bulk_buf_fini(buf);
	c2_mutex_unlock(&rbulk->rb_mutex);
}

const struct c2_net_buffer_callbacks rpc_bulk_cb  = {
	.nbc_cb = {
		[C2_NET_QT_PASSIVE_BULK_SEND] = rpc_bulk_buf_cb,
		[C2_NET_QT_PASSIVE_BULK_RECV] = rpc_bulk_buf_cb,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = rpc_bulk_buf_cb,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = rpc_bulk_buf_cb,
	}
};

void c2_rpc_bulk_init(struct c2_rpc_bulk *rbulk)
{
	C2_PRE(rbulk != NULL);

	rpcbulk_tlist_init(&rbulk->rb_buflist);
	c2_chan_init(&rbulk->rb_chan);
	c2_mutex_init(&rbulk->rb_mutex);
	rbulk->rb_magic = C2_RPC_BULK_MAGIC;
	rbulk->rb_bytes = 0;
	rbulk->rb_rc = 0;
}
C2_EXPORTED(c2_rpc_bulk_init);

void c2_rpc_bulk_fini(struct c2_rpc_bulk *rbulk)
{
	C2_PRE(rbulk != NULL);
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_PRE(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);
	C2_PRE(rpcbulk_tlist_is_empty(&rbulk->rb_buflist));

	c2_chan_fini(&rbulk->rb_chan);
	c2_mutex_fini(&rbulk->rb_mutex);
	rpcbulk_tlist_fini(&rbulk->rb_buflist);
}
C2_EXPORTED(c2_rpc_bulk_fini);

void c2_rpc_bulk_buflist_empty(struct c2_rpc_bulk *rbulk)
{
	struct c2_rpc_bulk_buf *buf;

	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(rpc_bulk_invariant(rbulk));
	c2_tl_for(rpcbulk, &rbulk->rb_buflist, buf) {
		rpcbulk_tlist_del(buf);
		rpc_bulk_buf_fini(buf);
	} c2_tl_endfor;
	c2_mutex_unlock(&rbulk->rb_mutex);
}

int c2_rpc_bulk_buf_add(struct c2_rpc_bulk *rbulk,
			uint32_t segs_nr,
			struct c2_net_domain *netdom,
			struct c2_net_buffer *nb,
			struct c2_rpc_bulk_buf **out)
{
	int			rc;
	struct c2_rpc_bulk_buf *buf;

	C2_PRE(rbulk != NULL);
	C2_PRE(netdom != NULL);
	C2_PRE(out != NULL);

	if (segs_nr > c2_net_domain_get_max_buffer_segments(netdom))
		return -EMSGSIZE;

	C2_ALLOC_PTR(buf);
	if (buf == NULL)
		return -ENOMEM;

	rc = rpc_bulk_buf_init(buf, segs_nr, nb);
	if (rc != 0) {
		c2_free(buf);
		return rc;
	}

	c2_mutex_lock(&rbulk->rb_mutex);
	buf->bb_rbulk = rbulk;
	rpcbulk_tlist_add_tail(&rbulk->rb_buflist, buf);
	C2_POST(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);
	*out = buf;
	C2_POST(rpc_bulk_buf_invariant(buf));
	return 0;
}
C2_EXPORTED(c2_rpc_bulk_buf_add);

int c2_rpc_bulk_buf_databuf_add(struct c2_rpc_bulk_buf *rbuf,
			        void *buf,
			        c2_bcount_t count,
			        c2_bindex_t index,
				struct c2_net_domain *netdom)
{
	int			 rc;
	struct c2_buf		 cbuf;
	struct c2_rpc_bulk	*rbulk;

	C2_PRE(rbuf != NULL);
	C2_PRE(rpc_bulk_buf_invariant(rbuf));
	C2_PRE(buf != NULL);
	C2_PRE(count != 0);
	C2_PRE(netdom != NULL);

	if (c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec) + count >
	    c2_net_domain_get_max_buffer_size(netdom) ||
	    count > c2_net_domain_get_max_buffer_segment_size(netdom))
		return -EMSGSIZE;

	cbuf.b_addr = buf;
	cbuf.b_nob = count;
	rbulk = rbuf->bb_rbulk;
	rc = c2_0vec_cbuf_add(&rbuf->bb_zerovec, &cbuf, &index);
	if (rc != 0)
		return rc;

	rbuf->bb_nbuf->nb_buffer = rbuf->bb_zerovec.z_bvec;
	C2_POST(rpc_bulk_buf_invariant(rbuf));
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_POST(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);
	return rc;
}
C2_EXPORTED(c2_rpc_bulk_buf_databuf_add);

void c2_rpc_bulk_qtype(struct c2_rpc_bulk *rbulk, enum c2_net_queue_type q)
{
	struct c2_rpc_bulk_buf *rbuf;

	C2_PRE(rbulk != NULL);
	C2_PRE(c2_mutex_is_locked(&rbulk->rb_mutex));
	C2_PRE(!rpcbulk_tlist_is_empty(&rbulk->rb_buflist));
	C2_PRE(q == C2_NET_QT_PASSIVE_BULK_RECV ||
	       q == C2_NET_QT_PASSIVE_BULK_SEND ||
	       q == C2_NET_QT_ACTIVE_BULK_RECV ||
	       q == C2_NET_QT_ACTIVE_BULK_SEND);

	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		rbuf->bb_nbuf->nb_qtype = q;
	} c2_tl_endfor;
}

static int rpc_bulk_op(struct c2_rpc_bulk *rbulk,
		       const struct c2_rpc_conn *conn,
		       struct c2_net_buf_desc *descs,
		       enum c2_rpc_bulk_op_type op)
{
	int				 rc;
	int				 cnt = 0;
	struct c2_rpc_bulk_buf		*rbuf;
	struct c2_net_transfer_mc	*tm;
	struct c2_net_buffer		*nb;
	struct c2_net_domain		*netdom;
	struct c2_rpc_machine		*rpcmach;

	C2_PRE(rbulk != NULL);
	C2_PRE(descs != NULL);
	C2_PRE(op == C2_RPC_BULK_STORE || op == C2_RPC_BULK_LOAD);

	rpcmach = conn->c_rpc_machine;
	tm = &rpcmach->rm_tm;
	netdom = tm->ntm_dom;
	c2_mutex_lock(&rbulk->rb_mutex);
	C2_ASSERT(rpc_bulk_invariant(rbulk));
	C2_ASSERT(!rpcbulk_tlist_is_empty(&rbulk->rb_buflist));

	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		nb = rbuf->bb_nbuf;
		nb->nb_length = c2_vec_count(&rbuf->bb_zerovec.z_bvec.ov_vec);
		C2_ASSERT(rpc_bulk_buf_invariant(rbuf));
		if (op == C2_RPC_BULK_STORE) {
			C2_ASSERT(nb->nb_qtype ==
				  C2_NET_QT_PASSIVE_BULK_RECV ||
				  nb->nb_qtype ==
				  C2_NET_QT_PASSIVE_BULK_SEND);
		} else
			C2_ASSERT(nb->nb_qtype ==
				  C2_NET_QT_ACTIVE_BULK_RECV ||
				  nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND);
		nb->nb_callbacks = &rpc_bulk_cb;

		/*
		 * Registers the net buffer with net domain if it is not
		 * registered already.
		 */
		if (!(nb->nb_flags & C2_NET_BUF_REGISTERED)) {
			rc = c2_net_buffer_register(nb, netdom);
			if (rc != 0) {
				C2_ADDB_ADD(&rpcmach->rm_rpc_machine_addb,
					    &rpc_machine_addb_loc,
					    rpc_machine_func_fail,
					    "Net buf registration failed.", rc);
				goto cleanup;
			}
			rbuf->bb_flags |= C2_RPC_BULK_NETBUF_REGISTERED;
		}

		if (op == C2_RPC_BULK_LOAD) {
			rc = c2_net_desc_copy(&descs[cnt], &nb->nb_desc);
			if (rc != 0) {
				C2_ADDB_ADD(&rpcmach->rm_rpc_machine_addb,
					    &rpc_machine_addb_loc,
					    rpc_machine_func_fail,
					    "Load: Net buf desc copy failed.",
					    rc);
				if (rbuf->bb_flags &
				    C2_RPC_BULK_NETBUF_REGISTERED)
					c2_net_buffer_deregister(nb, netdom);
				goto cleanup;
			}
		}

		nb->nb_app_private = rbuf;
		rc = c2_net_buffer_add(nb, tm);
		if (rc != 0) {
			C2_ADDB_ADD(&rpcmach->rm_rpc_machine_addb,
				    &rpc_machine_addb_loc,
				    rpc_machine_func_fail,
				    "Buffer addition to TM failed.", rc);
			if (rbuf->bb_flags & C2_RPC_BULK_NETBUF_REGISTERED)
				c2_net_buffer_deregister(nb, netdom);
			goto cleanup;
		}

		if (op == C2_RPC_BULK_STORE) {
			rc = c2_net_desc_copy(&nb->nb_desc, &descs[cnt]);
                        if (rc != 0) {
                                C2_ADDB_ADD(&rpcmach->rm_rpc_machine_addb,
                                            &rpc_machine_addb_loc,
                                            rpc_machine_func_fail,
                                            "Store: Net buf desc copy failed.",
                                            rc);
                                c2_net_buffer_del(nb, tm);
                                goto cleanup;
                        }
		}

		++cnt;
		rbulk->rb_bytes += c2_vec_count(&rbuf->bb_zerovec.z_bvec.
						ov_vec);
	} c2_tl_endfor;
	C2_POST(rpc_bulk_invariant(rbulk));
	c2_mutex_unlock(&rbulk->rb_mutex);

	return rc;
cleanup:
	C2_ASSERT(rc != 0);
	rpcbulk_tlist_del(rbuf);
	c2_tl_for(rpcbulk, &rbulk->rb_buflist, rbuf) {
		if (rbuf->bb_flags & C2_RPC_BULK_NETBUF_REGISTERED)
			c2_net_buffer_deregister(rbuf->bb_nbuf, netdom);
		if (rbuf->bb_nbuf->nb_flags & C2_NET_BUF_QUEUED)
			c2_net_buffer_del(rbuf->bb_nbuf, tm);
	} c2_tl_endfor;
	c2_mutex_unlock(&rbulk->rb_mutex);
	return rc;
}

int c2_rpc_bulk_store(struct c2_rpc_bulk *rbulk,
		      const struct c2_rpc_conn *conn,
		      struct c2_net_buf_desc *to_desc)
{
	return rpc_bulk_op(rbulk, conn, to_desc, C2_RPC_BULK_STORE);
}
C2_EXPORTED(c2_rpc_bulk_store);

int c2_rpc_bulk_load(struct c2_rpc_bulk *rbulk,
		     const struct c2_rpc_conn *conn,
		     struct c2_net_buf_desc *from_desc)
{
	return rpc_bulk_op(rbulk, conn, from_desc, C2_RPC_BULK_LOAD);
}
C2_EXPORTED(c2_rpc_bulk_load);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
