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
#include "rpc/rpccore.h"
#include "ioservice/io_fops.h"
#include "rpc/rpcdbg.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "rpc/session.h"
#include "rpc/session_internal.h"
#include "fop/fop.h"
#include "rpc/formation.h"
#include "fid/fid.h"
#ifndef __KERNEL__
#include "rpc/it/ping_fop.h"
#endif
#include "rpc/rpc_onwire.h"

struct c2_fop_io_vec;

/* ADDB Instrumentation for rpccore. */
static const struct c2_addb_ctx_type rpc_machine_addb_ctx_type = {
	        .act_name = "rpc-machine"
};

static const struct c2_addb_loc rpc_machine_addb_loc = {
	        .al_name = "rpc-machine"
};

C2_ADDB_EV_DEFINE(rpc_machine_func_fail, "rpc_machine_func_fail",
		                C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/** onwire_fmt List of all known rpc item types 
static struct c2_list  rpc_item_types_list;

Lock for list of item types 
static struct c2_rwlock rpc_item_types_lock;
*/

static void rpc_net_buf_received(const struct c2_net_buffer_event *ev);

/**
   Buffer callback for buffers added by rpc layer for receiving messages.
 */
struct c2_net_buffer_callbacks c2_rpc_rcv_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV] = rpc_net_buf_received,
	}
};

void c2_rpc_frm_net_buffer_sent(const struct c2_net_buffer_event *ev);

/**
   Callback for net buffer used in posting
 */
struct c2_net_buffer_callbacks c2_rpc_send_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_SEND] = c2_rpc_frm_net_buffer_sent,
	}
};

static void rpc_tm_event_cb(const struct c2_net_tm_event *ev)
{
}

int c2_rpc_decode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb);

/**
   Transfer machine callback vector for transfer machines created by
   rpc layer.
 */
struct c2_net_tm_callbacks c2_rpc_tm_callbacks = {
	.ntc_event_cb = rpc_tm_event_cb
};

static const struct c2_update_stream_ops update_stream_ops;
static const struct c2_rpc_item_type_ops rpc_item_ops;

void c2_rpc_rpcobj_init(struct c2_rpc *rpc)
{
	C2_PRE(rpc != NULL);

	c2_list_link_init(&rpc->r_linkage);
	c2_list_init(&rpc->r_items);
	rpc->r_session = NULL;
}

static int update_stream_init(struct c2_update_stream *us,
			       struct c2_rpcmachine *mach)
{
	us->us_session_id = ~0;
	us->us_slot_id    = ~0;

	us->us_ops   = &update_stream_ops;
	us->us_mach  = mach;
	us->us_state = UPDATE_STREAM_UNINITIALIZED;

	c2_mutex_init(&us->us_guard);
	return 0;
}

static void update_stream_fini(struct c2_update_stream *us)
{
	c2_mutex_fini(&us->us_guard);
}

static int rpc_init(struct c2_rpc *rpc) __attribute__((unused)); /*XXX: for now*/
static int rpc_init(struct c2_rpc *rpc)
{
	c2_list_link_init(&rpc->r_linkage);
	c2_list_init(&rpc->r_items);
	rpc->r_session = NULL;
	return 0;
}

void c2_rpc_rpcobj_fini(struct c2_rpc *rpc)
{
	rpc->r_session = NULL;
	c2_list_fini(&rpc->r_items);
	c2_list_link_fini(&rpc->r_linkage);
}

/* can be exported, used c2_ prefix */
static void c2_rpc_item_fini(struct c2_rpc_item *item)
{
	item->ri_state = RPC_ITEM_FINALIZED;
	c2_chan_fini(&item->ri_chan);
}

static void c2_rpc_item_ref_fini(struct c2_ref *ref)
{
	struct c2_rpc_item *item;
	item = container_of(ref, struct c2_rpc_item, ri_ref);
	c2_rpc_item_fini(item);
}

int c2_rpc_item_init(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	*sref;

	C2_SET0(item);
	item->ri_magic = C2_RPC_ITEM_MAGIC;
	c2_chan_init(&item->ri_chan);
        c2_list_link_init(&item->ri_linkage);
	c2_ref_init(&item->ri_ref, 1, c2_rpc_item_ref_fini);
	item->ri_state = RPC_ITEM_UNINITIALIZED;

	sref = &item->ri_slot_refs[0];
	sref->sr_slot_id = SLOT_ID_INVALID;
	c2_list_link_init(&sref->sr_link);
	c2_list_link_init(&sref->sr_ready_link);

        c2_list_link_init(&item->ri_unbound_link);
	item->ri_slot_refs[0].sr_sender_id = SENDER_ID_INVALID;
	item->ri_slot_refs[0].sr_session_id = SESSION_ID_INVALID;

        c2_list_link_init(&item->ri_rpcobject_linkage);
	c2_list_link_init(&item->ri_unformed_linkage);
        c2_list_link_init(&item->ri_group_linkage);

	return 0;
}

int c2_rpc_post(struct c2_rpc_item	*item)
{
	int		res = 0;

	C2_ASSERT(item != NULL && item->ri_session != NULL &&
		  (item->ri_session->s_state == C2_RPC_SESSION_IDLE ||
		   item->ri_session->s_state == C2_RPC_SESSION_BUSY));

	c2_time_now(&item->ri_rpc_entry_time);

	/*printf("item_post: item %p session %p(%lu)\n", item, item->ri_session,
			item->ri_session->s_session_id);*/
	item->ri_state = RPC_ITEM_SUBMITTED;
	item->ri_mach = item->ri_session->s_conn->c_rpcmachine;
	item->ri_type->rit_flags = C2_RPC_ITEM_UNBOUND;
	res = c2_rpc_frm_ubitem_added(item);
	return res;
}

int c2_rpc_reply_post(struct c2_rpc_item	*request,
		      struct c2_rpc_item	*reply)
{
	struct c2_rpc_slot_ref	*sref;
	struct c2_rpc_item	*tmp;
	struct c2_rpc_slot	*slot;

	C2_PRE(request != NULL && reply != NULL);
	C2_PRE(request->ri_tstate == RPC_ITEM_IN_PROGRESS);

	c2_time_now(&reply->ri_rpc_entry_time);

	reply->ri_session = request->ri_session;
	reply->ri_slot_refs[0].sr_sender_id =
		request->ri_slot_refs[0].sr_sender_id;
	reply->ri_slot_refs[0].sr_session_id =
		request->ri_slot_refs[0].sr_session_id;
	reply->ri_slot_refs[0].sr_uuid = request->ri_slot_refs[0].sr_uuid;
	reply->ri_prio = request->ri_prio;
	reply->ri_deadline = request->ri_deadline;
	reply->ri_error = 0;
	reply->ri_state = RPC_ITEM_SUBMITTED;

	sref = &reply->ri_slot_refs[0];

	slot = sref->sr_slot = request->ri_slot_refs[0].sr_slot;
	sref->sr_item = reply;

	sref->sr_slot_id = request->ri_slot_refs[0].sr_slot_id;
	sref->sr_verno = request->ri_slot_refs[0].sr_verno;
	sref->sr_xid = request->ri_slot_refs[0].sr_xid;
	sref->sr_slot_gen = request->ri_slot_refs[0].sr_slot_gen;

	reply->ri_mach = reply->ri_session->s_conn->c_rpcmachine;
	request->ri_mach = request->ri_session->s_conn->c_rpcmachine;

	reply->ri_type->rit_flags = C2_RPC_ITEM_BOUND;

	c2_mutex_lock(&slot->sl_mutex);
	c2_rpc_slot_reply_received(reply->ri_slot_refs[0].sr_slot,
				   reply, &tmp);
	c2_mutex_unlock(&slot->sl_mutex);
	return 0;
}

bool c2_rpc_item_is_update(struct c2_rpc_item *item)
{
	return item->ri_type->rit_mutabo;
}

bool c2_rpc_item_is_request(struct c2_rpc_item *item)
{
	return item->ri_type->rit_item_is_req;
}

bool c2_rpc_item_is_bound(struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);
	C2_PRE(item->ri_type != NULL);

	return item->ri_type->rit_flags & C2_RPC_ITEM_BOUND;
}

bool c2_rpc_item_is_unbound(struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);
	C2_PRE(item->ri_type != NULL);

	return item->ri_type->rit_flags & C2_RPC_ITEM_UNBOUND;
}

bool c2_rpc_item_is_unsolicited(struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);
	C2_PRE(item->ri_type != NULL);

	return item->ri_type->rit_flags & C2_RPC_ITEM_UNSOLICITED;
}

int c2_rpc_unsolicited_item_post(struct c2_rpc_conn *conn,
		struct c2_rpc_item *item)
{
	c2_time_t		 now;
	struct c2_rpc_session	*session_zero;

	C2_PRE(conn != NULL);
	C2_PRE(item != NULL);

	session_zero = c2_rpc_conn_session0(conn);

	item->ri_session = session_zero;
	item->ri_state = RPC_ITEM_SUBMITTED;
	item->ri_mach = item->ri_session->s_conn->c_rpcmachine;
	item->ri_type->rit_flags = C2_RPC_ITEM_UNSOLICITED;

	c2_time_now(&now);
	item->ri_rpc_entry_time = now;
	return c2_rpc_frm_ubitem_added(item);
}

void c2_rpc_ep_aggr_init(struct c2_rpc_ep_aggr *ep_aggr)
{
	C2_PRE(ep_aggr != NULL);

	c2_mutex_init(&ep_aggr->ea_mutex);
	c2_list_init(&ep_aggr->ea_chan_list);
}

void c2_rpc_ep_aggr_fini(struct c2_rpc_ep_aggr *ep_aggr)
{
	C2_PRE(ep_aggr != NULL);

	/* By this time, all elements of list should have been finied
	   and the list should be empty.*/
	c2_mutex_lock(&ep_aggr->ea_mutex);
	c2_list_fini(&ep_aggr->ea_chan_list);
	c2_mutex_unlock(&ep_aggr->ea_mutex);
	c2_mutex_fini(&ep_aggr->ea_mutex);
}

int c2_rpc_core_init(void)
{
	/* onwire_fmt c2_list_init(&rpc_item_types_list);
	c2_rwlock_init(&rpc_item_types_lock);*/
	return c2_rpc_session_module_init();
}
C2_EXPORTED(c2_rpc_core_init);

void c2_rpc_core_fini(void)
{
	c2_rpc_session_module_fini();
}
C2_EXPORTED(c2_rpc_core_fini);

static void rpc_chan_ref_release(struct c2_ref *ref)
{
	struct c2_rpc_chan		*chan = NULL;

	C2_PRE(ref != NULL);

	chan = container_of(ref, struct c2_rpc_chan, rc_ref);
	C2_ASSERT(chan != NULL);
	C2_PRE(c2_mutex_is_locked(&chan->rc_rpcmachine->cr_ep_aggr.ea_mutex));

	/* Destroy the chan structure. */
	c2_rpc_chan_destroy(chan->rc_rpcmachine, chan);
}

int c2_rpc_chan_create(struct c2_rpc_chan **chan, struct c2_rpcmachine *machine,
		struct c2_net_domain *net_dom, const char *ep_addr)
{
	int			 rc = 0;
	struct c2_rpc_chan	*ch = NULL;
	struct c2_clink		 tmwait;
	c2_bcount_t		 max_bufsize;
	c2_bcount_t		 max_segs_nr;

	C2_PRE(chan != NULL);
	C2_PRE(machine != NULL);
	C2_PRE(net_dom != NULL);
	C2_PRE(ep_addr != NULL);

	/* Allocate new rpc chan.*/
	C2_ALLOC_PTR(ch);
	if (ch == NULL) {
		C2_ADDB_ADD(&machine->cr_rpc_machine_addb,
				&rpc_machine_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}

	/* Allocate space for pointers of recv net buffers. */
	C2_ALLOC_ARR(ch->rc_rcv_buffers, C2_RPC_TM_RECV_BUFFERS_NR);
	if (ch->rc_rcv_buffers == NULL) {
		C2_ADDB_ADD(&machine->cr_rpc_machine_addb,
				&rpc_machine_addb_loc, c2_addb_oom);
		c2_free(ch);
		return -ENOMEM;
	}

	c2_ref_init(&ch->rc_ref, 1, rpc_chan_ref_release);

	ch->rc_tm.ntm_state = C2_NET_TM_UNDEFINED;
	ch->rc_tm.ntm_callbacks = &c2_rpc_tm_callbacks;
	ch->rc_rpcmachine = machine;

	/* Initialize the net transfer machine. */
	rc = c2_net_tm_init(&ch->rc_tm, net_dom);
	if (rc < 0) {
		c2_free(ch->rc_rcv_buffers);
		c2_free(ch);
		return rc;
	}

	/* Start the transfer machine so that users of this rpcmachine
	   can send/receive messages. */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&ch->rc_tm.ntm_chan, &tmwait);

	rc = c2_net_tm_start(&ch->rc_tm, ep_addr);
	if (rc < 0) {
		c2_clink_del(&tmwait);
		c2_clink_fini(&tmwait);
		goto cleanup;
	}

	/* Wait on transfer machine channel till transfer machine is
	   actually started. */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	/* If tm fails to start, propogate the error back. */
	if (ch->rc_tm.ntm_state != C2_NET_TM_STARTED)
		goto cleanup;

	/* Add buffers for receiving messages to this transfer machine. */
	rc = c2_rpc_net_recv_buffer_allocate_nr(net_dom, &ch->rc_tm);
	if (rc < 0) {
		c2_rpc_chan_destroy(machine, ch);
		return rc;
	}

	/* Add the new rpc chan structure to list of such structures in
	   rpcmachine. */
	c2_mutex_lock(&machine->cr_ep_aggr.ea_mutex);
	/* By default, all new channels are added at the end of list.
	   At the head, there is a c2_rpc_chan, which was added by
	   c2_rpcmachine_init and its refcount needs to be decremented
	   during c2_rpcmachine_fini, hence this arrangement. */
	c2_list_add_tail(&machine->cr_ep_aggr.ea_chan_list, &ch->rc_linkage);
	c2_mutex_unlock(&machine->cr_ep_aggr.ea_mutex);
	*chan = ch;
	/* Initialize the formation state machine attached with given
	   c2_rpc_chan structure. This state machine is finiied when
	   corresponding c2_rpc_chan is destroyed. */
	c2_rpc_frm_sm_init(ch, &machine->cr_formation, &ch->rc_frmsm);

	/* Assign network specific thresholds on max buffer size and
	   max number of fragments. */
	max_bufsize = c2_net_domain_get_max_buffer_size(net_dom);
	max_segs_nr = c2_net_domain_get_max_buffer_segments(net_dom);
	c2_rpc_frm_sm_net_limits_set(&ch->rc_frmsm, max_bufsize, max_segs_nr);
	return rc;
cleanup:
	c2_free(ch->rc_rcv_buffers);
	if (ch->rc_tm.ntm_state <= C2_NET_TM_STARTING)
		c2_net_tm_fini(&ch->rc_tm);
	c2_free(ch);
	return rc;
}

struct c2_rpc_chan *c2_rpc_chan_get(struct c2_rpcmachine *machine)
{
	struct c2_rpc_chan	*chan = NULL;
	struct c2_rpc_chan	*chan_found = NULL;
	int64_t			 ref;

	C2_PRE(machine != NULL);

	ref = INT64_MAX;

	/* Get a chan from chan list */
	c2_mutex_lock(&machine->cr_ep_aggr.ea_mutex);
	c2_list_for_each_entry(&machine->cr_ep_aggr.ea_chan_list, chan,
			struct c2_rpc_chan, rc_linkage) {
		if (c2_atomic64_get(&chan->rc_ref.ref_cnt) <= ref) {
			chan_found = chan;
			ref = c2_atomic64_get(&chan->rc_ref.ref_cnt);
		}
	}

	C2_POST(chan_found != NULL);
	c2_ref_get(&chan_found->rc_ref);
	c2_mutex_unlock(&machine->cr_ep_aggr.ea_mutex);
	return chan_found;
}

void c2_rpc_chan_put(struct c2_rpc_chan *chan)
{
	C2_PRE(chan != NULL);

	c2_mutex_lock(&chan->rc_rpcmachine->cr_ep_aggr.ea_mutex);
	c2_ref_put(&chan->rc_ref);
	c2_mutex_unlock(&chan->rc_rpcmachine->cr_ep_aggr.ea_mutex);
}

void c2_rpc_chan_destroy(struct c2_rpcmachine *machine,
		struct c2_rpc_chan *chan)
{
	int		i;
	int		rc;
	struct c2_clink	tmwait;

	C2_PRE(chan != NULL);
	C2_PRE(c2_mutex_is_locked(&machine->cr_ep_aggr.ea_mutex));

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&chan->rc_tm.ntm_chan, &tmwait);

	/* Fini the formation state machine since it can add net buffers
	   to the transfer machine. */
	c2_rpc_frm_sm_fini(&chan->rc_frmsm);

	rc = c2_net_tm_stop(&chan->rc_tm, false);
	if (rc < 0) {
		c2_clink_del(&tmwait);
		c2_clink_fini(&tmwait);
		C2_ADDB_ADD(&chan->rc_rpcmachine->cr_rpc_machine_addb,
				&rpc_machine_addb_loc, rpc_machine_func_fail,
				"c2_net_tm_stop", 0);
		return;
	}

	/* Wait for transfer machine to stop. */
	while (chan->rc_tm.ntm_state != C2_NET_TM_STOPPED)
		c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);
	c2_clink_fini(&tmwait);

	/* Delete all the buffers from net domain. */
	c2_rpc_net_recv_buffer_deallocate_nr(chan, false,
			C2_RPC_TM_RECV_BUFFERS_NR);

	/* Remove chan from list of such structures in rpcmachine. */
	c2_list_del(&chan->rc_linkage);

	/* Fini the transfer machine here and deallocate the chan. */
	c2_net_tm_fini(&chan->rc_tm);
	for (i = 0; i < C2_RPC_TM_RECV_BUFFERS_NR; ++i)
		C2_ASSERT(chan->rc_rcv_buffers[i] == NULL);

	c2_free(chan->rc_rcv_buffers);
	c2_free(chan);
}

int c2_rpc_reply_timedwait(struct c2_rpc_item *item, const c2_time_t *timeout)
{
	return 0;
}

int c2_rpc_group_timedwait(struct c2_rpc_group *group, const c2_time_t *timeout)
{
	return 0;
}

int c2_rpc_update_stream_get(struct c2_rpcmachine *machine,
			     struct c2_service_id *srvid,
			     enum c2_update_stream_flags flag,
			     const struct c2_update_stream_ops *ops,
			     struct c2_update_stream **out)
{
	int rc = -ENOMEM;

	C2_ALLOC_PTR(*out);
	if (*out == NULL)
		return rc;

	rc = update_stream_init(*out, machine);
	if (rc < 0)
		c2_free(*out);

	if (ops != NULL) {
		(*out)->us_ops = ops;
	}

	return rc;
}

void c2_rpc_update_stream_put(struct c2_update_stream *us)
{
	update_stream_fini(us);
	c2_free(us);
}

size_t c2_rpc_cache_item_count(struct c2_rpcmachine *machine,
			       enum c2_rpc_item_priority prio)
{
	return 0;
}

size_t c2_rpc_rpc_count(struct c2_rpcmachine *machine)
{
	return 0;
}

void c2_rpc_avg_rpc_item_time(struct c2_rpcmachine *machine,
			      c2_time_t *time)
{
}

size_t c2_rpc_bytes_per_sec(struct c2_rpcmachine *machine)
{
	return 0;
}

static int rpc_proc_ctl_init(struct c2_rpc_processing_ctl *ctl)
{
	return 0;
}

static void rpc_proc_ctl_fini(struct c2_rpc_processing_ctl *ctl)
{
}

static int rpc_proc_init(struct c2_rpc_processing *proc)
{
	int rc;
	rc = rpc_proc_ctl_init(&proc->crp_ctl);
	if (rc < 0)
		return rc;

	c2_list_init(&proc->crp_formation_lists);
	c2_list_init(&proc->crp_form);

	return rc;
}

static void rpc_proc_fini(struct c2_rpc_processing *proc)
{
	c2_list_fini(&proc->crp_form);
	c2_list_fini(&proc->crp_formation_lists);
	rpc_proc_ctl_fini(&proc->crp_ctl);
}

/**
   The callback routine to be called once the transfer machine
   receives a buffer. This subroutine later invokes decoding of
   net buffer and then notifies sessions component about every
   incoming rpc item.
 */
static void rpc_net_buf_received(const struct c2_net_buffer_event *ev)
{
	struct c2_rpc		 rpc;
	struct c2_rpc_item	*item = NULL;
	struct c2_net_buffer	*nb = NULL;
	struct c2_rpc_chan	*chan = NULL;
	int			 rc = 0;
	int			 i = 0;
	c2_time_t		 now;
	bool			 in_flight_dec = false;

	C2_PRE((ev != NULL) && (ev->nbe_buffer != NULL));

	/* Decode the buffer, get an RPC from it, traverse the
	   list of rpc items from that rpc and post reply callbacks
	   for each rpc item. */
	nb = ev->nbe_buffer;
	nb->nb_length = ev->nbe_length;
	nb->nb_ep = ev->nbe_ep;
	c2_rpc_rpcobj_init(&rpc);

	if (ev->nbe_status == 0) {
		c2_time_now(&now);
		rc = c2_rpc_decode(&rpc, nb);
		if (rc < 0) {
			/* XXX We can post an ADDB event here. */
		} else {
			printf("%lu rpc items received.\n",
				c2_list_length(&rpc.r_items));
			c2_list_for_each_entry(&rpc.r_items, item,
					struct c2_rpc_item,
					ri_rpcobject_linkage) {
				/* If this is a reply type rpc item, call a
				   sessions/slots method on it which will find
				   out its corresponding request item and call
				   its completion callback.*/
				chan = container_of(nb->nb_tm,
						struct c2_rpc_chan, rc_tm);
				item->ri_mach = chan->rc_rpcmachine;
				nb->nb_ep = ev->nbe_ep;
				item->ri_src_ep = nb->nb_ep;
				item->ri_rpc_entry_time = now;
				c2_rpc_item_attach(item);
				rc = c2_rpc_item_received(item);
				if (rc == 0 && !in_flight_dec) {
					in_flight_dec = true;
					if (!c2_rpc_item_is_conn_establish(item))
						c2_rpc_frm_rpcs_inflight_dec(
								item);
					/* Post an ADDB event here.*/
					++i;
				}
			}
		}
	}

	/* Add the c2_net_buffer back to the queue of
	   transfer machine. */
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_ep = NULL;
	nb->nb_callbacks = &c2_rpc_rcv_buf_callbacks;
	if (nb->nb_tm->ntm_state == C2_NET_TM_STARTED)
		rc = c2_net_buffer_add(nb, nb->nb_tm);
}

static int rpc_net_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nbuf, enum c2_net_queue_type qtype,
		uint64_t rpc_size)
{
	int				 rc;
	int32_t				 segs_nr;
	c2_bcount_t			 seg_size;
	c2_bcount_t			 buf_size;
	c2_bcount_t			 nrsegs;
	struct c2_net_buffer		*nb = NULL;

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
	if (qtype == C2_NET_QT_MSG_RECV)
		nb->nb_callbacks = &c2_rpc_rcv_buf_callbacks;
	else
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

int c2_rpc_net_recv_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nb)
{
	C2_PRE(net_dom != NULL);
	C2_PRE(nb != NULL);

	return rpc_net_buffer_allocate(net_dom, nb, C2_NET_QT_MSG_RECV, 0);
}

int c2_rpc_net_send_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nb, uint64_t rpc_size)
{
	C2_PRE(net_dom != NULL);
	C2_PRE(nb != NULL);

	return rpc_net_buffer_allocate(net_dom, nb, C2_NET_QT_MSG_SEND,
			rpc_size);
}

int c2_rpc_net_recv_buffer_allocate_nr(struct c2_net_domain *net_dom,
		struct c2_net_transfer_mc *tm)
{
	int			 rc = 0;
	int			 st = 0;
	uint32_t		 i = 0;
	struct c2_net_buffer	*nb = NULL;
	struct c2_rpc_chan	*chan;

	C2_PRE(net_dom != NULL);

	chan = container_of(tm, struct c2_rpc_chan, rc_tm);
	C2_ASSERT(chan != NULL);

	for (i = 0; i < C2_RPC_TM_RECV_BUFFERS_NR; ++i) {
		rc = c2_rpc_net_recv_buffer_allocate(net_dom, &nb);
		if (rc != 0)
			break;
		chan->rc_rcv_buffers[i] = nb;
		rc = c2_net_buffer_add(nb, tm);
		if (rc < 0)
			break;
	}
	if (rc < 0) {
		st = c2_rpc_net_recv_buffer_deallocate_nr(chan, true, i);
		if (st < 0)
			return rc;
		c2_net_tm_fini(tm);
	}
	return rc;
}

int c2_rpc_net_recv_buffer_deallocate(struct c2_net_buffer *nb,
		struct c2_rpc_chan *chan, bool tm_active)
{
	int				 rc = 0;
	struct c2_clink			 tmwait;
	struct c2_net_transfer_mc	*tm;
	struct c2_net_domain		*net_dom;

	C2_PRE(nb != NULL);
	C2_PRE(chan != NULL);

	tm = &chan->rc_tm;
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
	return rc;
}

int c2_rpc_net_send_buffer_deallocate(struct c2_net_buffer *nb,
		struct c2_net_domain *net_dom)
{
	int		rc = 0;

	C2_PRE(nb != NULL);
	C2_PRE(net_dom != NULL);

	c2_net_buffer_deregister(nb, net_dom);
	c2_bufvec_free(&nb->nb_buffer);
	return rc;
}

int c2_rpc_net_recv_buffer_deallocate_nr(struct c2_rpc_chan *chan,
		bool tm_active, uint32_t nr)
{
	int			 i;
	int			 rc = 0;
	struct c2_net_buffer	*nb = NULL;

	C2_PRE(chan != NULL);
	C2_PRE(nr <= C2_RPC_TM_RECV_BUFFERS_NR);

	for (i = 0; i < nr; ++i) {
		nb = chan->rc_rcv_buffers[i];
		rc = c2_rpc_net_recv_buffer_deallocate(nb, chan, tm_active);
		if (rc < 0) {
			break;
		}
		chan->rc_rcv_buffers[i] = NULL;
	}
	return rc;
}

int c2_rpcmachine_init(struct c2_rpcmachine	*machine,
		struct c2_cob_domain	*dom,
		struct c2_net_domain	*net_dom,
		const char		*ep_addr)
{
	struct c2_db_tx			 tx;
	struct c2_cob			*root_session_cob;
	int				 rc;
	struct c2_rpc_chan		*chan;

	/* The c2_net_domain is expected to be created by end user.*/
	C2_PRE(machine != NULL);
	C2_PRE(dom != NULL);
	C2_PRE(ep_addr != NULL);
	C2_PRE(net_dom != NULL);

	c2_mutex_init(&machine->cr_stats_mutex);

	rc = rpc_proc_init(&machine->cr_processing);
	if (rc < 0)
		return rc;

	c2_list_init(&machine->cr_incoming_conns);
	c2_list_init(&machine->cr_outgoing_conns);
	c2_mutex_init(&machine->cr_session_mutex);
	c2_list_init(&machine->cr_ready_slots);
	c2_mutex_init(&machine->cr_ready_slots_mutex);

	machine->cr_dom = dom;
	c2_db_tx_init(&tx, dom->cd_dbenv, 0);
	rc = c2_rpc_root_session_cob_create(dom, &root_session_cob, &tx);
	if (rc == 0)
		c2_db_tx_commit(&tx);
	else
		c2_db_tx_abort(&tx);

	/* Create new c2_rpc_chan structure, init the transfer machine
	   passing the source endpoint. */
	c2_rpc_ep_aggr_init(&machine->cr_ep_aggr);

	/* Initialize the formation module. */
	rc = c2_rpc_frm_init(&machine->cr_formation);
	if (rc < 0) {
		c2_rpc_ep_aggr_fini(&machine->cr_ep_aggr);
		return rc;
	}

	rc = c2_rpc_chan_create(&chan, machine, net_dom, ep_addr);
	if (rc < 0) {
		c2_rpc_ep_aggr_fini(&machine->cr_ep_aggr);
		return rc;
	}

	/* Init the add context for this rpcmachine */
	c2_addb_ctx_init(&machine->cr_rpc_machine_addb,
			&rpc_machine_addb_ctx_type, &c2_addb_global_ctx);
	c2_addb_choose_default_level(AEL_WARN);

	return rc;
}

/**
   XXX Temporary. This routine will be discarded, once rpc-core starts
   providing c2_rpc_item::ri_ops::rio_sent() callback.

   In-memory state of conn should be cleaned up when reply to CONN_TERMINATE
   has been sent. As of now, rpc-core does not provide this callback. So this
   is a temporary routine, that cleans up all terminated connections from
   rpc connection list maintained in rpcmachine.
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

void c2_rpcmachine_fini(struct c2_rpcmachine *machine)
{
	struct c2_rpc_chan	*chan = NULL;

	C2_PRE(machine != NULL);

	rpc_proc_fini(&machine->cr_processing);
	conn_list_fini(&machine->cr_incoming_conns);
	conn_list_fini(&machine->cr_outgoing_conns);
	c2_list_fini(&machine->cr_ready_slots);
	c2_mutex_fini(&machine->cr_session_mutex);
	/* Release the reference on the source endpoint and the
	   concerned c2_rpc_chan here.
	   The chan structure at head of list is the one that was added
	   during rpcmachine_init and its reference has to be released here. */
	chan = c2_list_entry(c2_list_first(&machine->cr_ep_aggr.ea_chan_list),
			struct c2_rpc_chan, rc_linkage);
	c2_rpc_chan_put(chan);
	c2_rpc_ep_aggr_fini(&machine->cr_ep_aggr);
	c2_mutex_fini(&machine->cr_stats_mutex);
	c2_addb_ctx_fini(&machine->cr_rpc_machine_addb);
}

/** simple vector of RPC-item operations */
static void rpc_item_op_sent(struct c2_rpc_item *item)
{
	//DBG("item: xid: %lu, SENT\n", item->ri_verno.vn_vc);
}

static void rpc_item_op_added(struct c2_rpc *rpc, struct c2_rpc_item *item)
{
	//DBG("item: xid: %lu, ADDED\n", item->ri_verno.vn_vc);
}

static void rpc_item_op_replied(struct c2_rpc_item *item, int rc)
{
	//DBG("item: xid: %lu, REPLIED\n", item->ri_verno.vn_vc);
}

static const struct c2_rpc_item_type_ops rpc_item_ops = {
	.rito_sent    = rpc_item_op_sent,
	.rito_added   = rpc_item_op_added,
	.rito_replied = rpc_item_op_replied
};

/** simple vector of update stream operations */
void us_timeout(struct c2_update_stream *us)
{
	//DBG("us: ssid: %lu, slotid: %lu, TIMEOUT\n", us->us_session_id, us->us_slot_id);
}

void us_recovery_complete(struct c2_update_stream *us)
{
	//DBG("us: ssid: %lu, slotid: %lu, RECOVERED\n", us->us_session_id, us->us_slot_id);
}

/**
   rio_replied op from rpc type ops.
   If this is an IO request, free the IO vector
   and free the fop.
 */
void c2_rpc_item_replied(struct c2_rpc_item *item, int rc)
{
	struct c2_fop			*fop = NULL;

	C2_PRE(item != NULL);
	/* Find out fop from the rpc item,
	   Find out opcode of rpc item,
	   Deallocate the io vector of rpc item accordingly.*/

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	if (fop->f_type->ft_ops->fto_fop_replied != NULL)
		fop->f_type->ft_ops->fto_fop_replied(fop);
}

/**
   RPC item ops function
   Function to return size of fop
 */
uint64_t c2_rpc_item_size(const struct c2_rpc_item *item)
{
	struct c2_fop			*fop = NULL;
	uint64_t			 size = 0;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	if(fop->f_type->ft_ops->fto_size_get != NULL)
		size = fop->f_type->ft_ops->fto_size_get(fop);
	else
		size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;

	return size;
}

/**
   Find if given 2 rpc items belong to same type or not.
 */
bool c2_rpc_item_equal(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{
	struct c2_fop		*fop1 = NULL;
	struct c2_fop		*fop2 = NULL;
	bool			 ret = false;

	C2_PRE(item1 != NULL);
	C2_PRE(item2 != NULL);

	fop1 = c2_rpc_item_to_fop(item1);
	fop2 = c2_rpc_item_to_fop(item2);
	ret = fop1->f_type->ft_ops->fto_op_equal(fop1, fop2);
	return ret;
}

bool c2_rpc_item_fid_equal(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{
	struct c2_fop		*fop1;
	struct c2_fop		*fop2;

	C2_PRE(item1 != NULL);
	C2_PRE(item2 != NULL);

	fop1 = c2_rpc_item_to_fop(item1);
	fop2 = c2_rpc_item_to_fop(item2);
	C2_ASSERT(fop1 != NULL);
	C2_ASSERT(fop2 != NULL);
	return fop1->f_type->ft_ops->fto_fid_equal(fop1, fop2);
}

/** onwire_fmt Registers a new rpc item type with the RPC subsystem 
void c2_rpc_item_type_add(struct c2_rpc_item_type *item_type)
{
	C2_PRE(item_type != NULL);

	c2_rwlock_write_lock(&rpc_item_types_lock);
	c2_list_add(&rpc_item_types_list, &item_type->rit_linkage);
	c2_rwlock_write_unlock(&rpc_item_types_lock);
}*/

/** onwire_fmt Returns an rpc item type for a specific rpc item opcode 
struct c2_rpc_item_type *c2_rpc_item_type_lookup(uint32_t opcode)
{
	struct c2_rpc_item_type *item_type;
	bool			 found = false;
	c2_rwlock_read_lock(&rpc_item_types_lock);
	c2_list_for_each_entry(&rpc_item_types_list, item_type,
	                       struct c2_rpc_item_type, rit_linkage) {
		if( item_type->rit_opcode == opcode) {
			found = true;
			break;
		}
	}
	c2_rwlock_read_unlock(&rpc_item_types_lock);
	if(found)
		return item_type;

	return NULL;
}
*/
struct c2_rpc_item_type *c2_rpc_item_type_lookup(uint32_t opcode)
{
	struct c2_fop_type    		*ftype;
	struct c2_rpc_item_type		*item_type;

	ftype = c2_fop_type_search(opcode);
	C2_ASSERT(ftype != NULL);
	C2_ASSERT(ftype->ft_ri_type != NULL);
	item_type = ftype->ft_ri_type;
	return item_type;
}

/**
   RPC item ops function
   Function to find out number of fragmented buffers in IO request
 */
uint64_t c2_rpc_item_get_io_fragment_count(struct c2_rpc_item *item)
{
	struct c2_fop			*fop;
	uint64_t			 nfragments = 0;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);

	nfragments = fop->f_type->ft_ops->fto_get_nfragments(fop);
	return nfragments;
}

static const struct c2_update_stream_ops update_stream_ops = {
	.uso_timeout           = us_timeout,
	.uso_recovery_complete = us_recovery_complete
};

void c2_rpc_item_vec_restore(struct c2_rpc_item *b_item,
		struct c2_fop *bkpfop)
{
	struct c2_fop *fop;

	C2_PRE(b_item != NULL);

	fop = c2_rpc_item_to_fop(b_item);
	C2_ASSERT(fop != NULL);
	fop->f_type->ft_ops->fto_iovec_restore(fop, bkpfop);
}

/**
   Coalesce rpc items that share same fid and intent(read/write)
   @param c_item - c2_rpc_frm_item_coalesced structure.
   @param b_item - Given bound rpc item.
   @retval - 0 if routine succeeds, -ve number(errno) otherwise.
 */
int c2_rpc_item_io_coalesce(struct c2_rpc_frm_item_coalesced *c_item,
		struct c2_rpc_item *b_item)
{
	int			 res = 0;
	struct c2_list		 fop_list;
	struct c2_fop		*fop = NULL;
	struct c2_fop		*fop_next = NULL;
	struct c2_fop		*b_fop = NULL;
	struct c2_rpc_item	*item = NULL;

	C2_PRE(b_item != NULL);
	C2_PRE(c_item != NULL);

	c2_list_init(&fop_list);
	c2_list_for_each_entry(&c_item->ic_member_list, item,
			struct c2_rpc_item, ri_coalesced_linkage) {
		fop = c2_rpc_item_to_fop(item);
		c2_list_add(&fop_list, &fop->f_link);
	}
	b_fop = container_of(b_item, struct c2_fop, f_item);

	/* Restore the original IO vector of resultant rpc item. */
	res = fop->f_type->ft_ops->fto_io_coalesce(&fop_list, b_fop,
			c_item->ic_bkpfop);

	c2_list_for_each_entry_safe(&fop_list, fop, fop_next,
			struct c2_fop, f_link)
		c2_list_del(&fop->f_link);

	c2_list_fini(&fop_list);
	if (res == 0)
		c_item->ic_resultant_item = b_item;
	return res;
}

static const struct c2_rpc_item_type_ops rpc_item_readv_type_ops = {
	.rito_sent = NULL,
	.rito_added = NULL,
	.rito_replied = c2_rpc_item_replied,
	.rito_iovec_restore = c2_rpc_item_vec_restore,
	.rito_item_size = c2_rpc_item_size,
	.rito_items_equal = c2_rpc_item_equal,
	.rito_fid_equal = c2_rpc_item_fid_equal,
	.rito_get_io_fragment_count = c2_rpc_item_get_io_fragment_count,
	.rito_io_coalesce = c2_rpc_item_io_coalesce,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

static const struct c2_rpc_item_type_ops rpc_item_writev_type_ops = {
	.rito_sent = NULL,
	.rito_added = NULL,
	.rito_replied = c2_rpc_item_replied,
	.rito_iovec_restore = c2_rpc_item_vec_restore,
	.rito_item_size = c2_rpc_item_size,
	.rito_items_equal = c2_rpc_item_equal,
	.rito_fid_equal = c2_rpc_item_fid_equal,
	.rito_get_io_fragment_count = c2_rpc_item_get_io_fragment_count,
	.rito_io_coalesce = c2_rpc_item_io_coalesce,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

static const struct c2_rpc_item_type_ops rpc_item_ping_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = c2_rpc_item_replied,
        .rito_item_size = c2_rpc_item_default_size,
        .rito_items_equal = c2_rpc_item_equal,
        .rito_get_io_fragment_count = NULL,
        .rito_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

static const struct c2_rpc_item_type_ops rpc_item_ping_rep_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = c2_rpc_item_replied,
        .rito_item_size = c2_rpc_item_default_size,
        .rito_items_equal = c2_rpc_item_equal,
        .rito_get_io_fragment_count = NULL,
        .rito_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

static struct c2_rpc_item_type rpc_item_type_readv = {
	.rit_ops = &rpc_item_readv_type_ops,
};

static struct c2_rpc_item_type rpc_item_type_writev = {
	.rit_ops = &rpc_item_writev_type_ops,
};

static struct c2_rpc_item_type rpc_item_type_ping = {
        .rit_ops = &rpc_item_ping_type_ops,
        .rit_mutabo = true,
        .rit_item_is_req = true,
};

static struct c2_rpc_item_type rpc_item_type_ping_rep = {
        .rit_ops = &rpc_item_ping_rep_type_ops,
        .rit_mutabo = false,
        .rit_item_is_req = false,
};

/**
   Attach the given rpc item with its corresponding item type.
   @param item - given rpc item.
 */
void c2_rpc_item_attach(struct c2_rpc_item *item)
{
	struct c2_fop		*fop = NULL;
	int			 opcode = 0;

	C2_PRE(item != NULL);

        fop = c2_rpc_item_to_fop(item);
        opcode = fop->f_type->ft_code;
        switch (opcode) {
	case C2_IOSERVICE_READV_OPCODE:
		item->ri_type = &rpc_item_type_readv;
		break;
	case C2_IOSERVICE_WRITEV_OPCODE:
		item->ri_type = &rpc_item_type_writev;
		break;
	case c2_fop_ping_opcode:
		item->ri_type = &rpc_item_type_ping;
		break;
	case c2_fop_ping_rep_opcode:
		item->ri_type = &rpc_item_type_ping_rep;
		break;
	default:
		C2_ASSERT(item->ri_type != NULL);
		break;
	};
}

/**
   Associate an rpc with its corresponding rpc_item_type.
   Since rpc_item_type by itself can not be uniquely identified,
   rather it is tightly bound to its fop_type, the fop_type_code
   is passed, based on which the rpc_item is associated with its
   rpc_item_type.
 */
void c2_rpc_item_type_attach(struct c2_fop_type *fopt)
{
	uint32_t			 opcode = 0;

	C2_PRE(fopt != NULL);

	/* XXX Needs to be implemented in a clean way. */
	/* This is a temporary approach to associate an rpc_item
	   with its rpc_item_type. It will be discarded once we
	   have a better mapping function for associating
	   rpc_item_type with an rpc_item. */
	opcode = fopt->ft_code;
	switch (opcode) {
	case C2_IOSERVICE_READV_OPCODE:
		fopt->ft_ri_type = &rpc_item_type_readv;
		break;
	case C2_IOSERVICE_WRITEV_OPCODE:
		fopt->ft_ri_type = &rpc_item_type_writev;
		break;
	case c2_fop_ping_opcode:
		fopt->ft_ri_type = &rpc_item_type_ping;
		break;
	case c2_fop_ping_rep_opcode:
		fopt->ft_ri_type = &rpc_item_type_ping_rep;
		break;
	default:
		/* FOP operations which need to set opcode should either
		   attach on their own, or use this subroutine. Hence default
		   is kept blank */
		break;
	};
	/* XXX : Assign unique opcode to associated rpc item type.
	 *       Will be removed once proper mapping and association between
	 *	 rpc item and fop is established
	 */
	if(fopt->ft_ri_type != NULL)
		fopt->ft_ri_type->rit_opcode = opcode;
}
/*
void c2_rpc_item_type_opcode_assign(struct c2_fop_type *fopt)
{
	uint32_t		opcode;

	C2_PRE(fopt != NULL);

	opcode = fopt->ft_code;
	if(fopt->ft_ri_type != NULL)
		fopt->ft_ri_type->rit_opcode = opcode;
}*/
/**
  Set the stats for outgoing rpc item
  @param item - incoming or outgoing rpc item
  @param path - enum distinguishing whether the item is incoming or outgoing
 */
void c2_rpc_item_exit_stats_set(struct c2_rpc_item *item,
		enum c2_rpc_item_path path)
{
	struct c2_rpc_stats		*st;

	C2_PRE(item != NULL);

	c2_time_now(&item->ri_rpc_exit_time);

	st = &item->ri_mach->cr_rpc_stats[path];
	c2_mutex_lock(&item->ri_mach->cr_stats_mutex);
        st->rs_i_lat = c2_time_sub(item->ri_rpc_exit_time,
                        item->ri_rpc_entry_time);
        if (st->rs_min_lat >= st->rs_i_lat || st->rs_min_lat == 0)
                st->rs_min_lat = st->rs_i_lat;
        if (st->rs_max_lat <= st->rs_i_lat || st->rs_max_lat == 0)
                st->rs_max_lat = st->rs_i_lat;

        st->rs_avg_lat = ((st->rs_items_nr * st->rs_avg_lat) +
                        st->rs_i_lat) / (st->rs_items_nr +1);
        st->rs_items_nr++;
        st->rs_bytes_nr += c2_rpc_item_default_size(item);

	c2_mutex_unlock(&item->ri_mach->cr_stats_mutex);
}


/* Dummy reqh queue of items */

struct c2_queue	c2_exec_queue;
struct c2_mutex c2_exec_queue_mutex;
struct c2_chan  c2_exec_chan;

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
