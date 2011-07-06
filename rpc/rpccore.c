#include "cob/cob.h"
#include "rpc/rpccore.h"
#include "rpc/rpcdbg.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "rpc/session.h"
#include "rpc/session_internal.h"
#include "fop/fop.h"
#include "formation.h"
#include "rpc/rpc_onwire.h"
#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

static void c2_rpc_net_buf_received(const struct c2_net_buffer_event *ev);

/**
   Buffer callback for buffers added by rpc layer for receiving messages.
 */
struct c2_net_buffer_callbacks c2_rpc_rcv_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV] = c2_rpc_net_buf_received,
	}
};

void c2_rpc_form_extevt_net_buffer_sent(const struct c2_net_buffer_event *ev);

/**
   Callback for net buffer used in posting
 */
struct c2_net_buffer_callbacks c2_rpc_send_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_SEND] = c2_rpc_form_extevt_net_buffer_sent,
	}
};

static void c2_rpc_tm_event_cb(const struct c2_net_tm_event *ev)
{
}

int c2_rpc_decode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb);

/**
   Transfer machine callback vector for transfer machines created by
   rpc layer.
 */
struct c2_net_tm_callbacks c2_rpc_tm_callbacks = {
	.ntc_event_cb = c2_rpc_tm_event_cb
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
static void rpc_fini(struct c2_rpc *rpc) __attribute__((unused)); /*XXX: for now*/
static void rpc_fini(struct c2_rpc *rpc)
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
	c2_time_t	now;
	int 		res = 0;

	C2_ASSERT(item != NULL && item->ri_session != NULL &&
		  (item->ri_session->s_state == C2_RPC_SESSION_IDLE ||
		   item->ri_session->s_state == C2_RPC_SESSION_BUSY));

	c2_time_now(&now);
	item->ri_rpc_entry_time = now;

	printf("item_post: item %p session %p(%lu)\n", item, item->ri_session,
			item->ri_session->s_session_id);
	item->ri_state = RPC_ITEM_SUBMITTED;
	item->ri_mach = item->ri_session->s_conn->c_rpcmachine;
	res = c2_rpc_form_extevt_unbounded_rpcitem_added(item);
	return res;
}
int c2_rpc_reply_post(struct c2_rpc_item	*request,
		      struct c2_rpc_item	*reply)
{
	struct c2_rpc_slot_ref	*sref;
	struct c2_rpc_item	*tmp;
	struct c2_rpc_slot	*slot;
	c2_time_t	now;

	C2_PRE(request != NULL && reply != NULL);
	C2_PRE(request->ri_tstate == RPC_ITEM_IN_PROGRESS);

	c2_time_now(&now);
	reply->ri_rpc_entry_time = now;

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

	c2_mutex_lock(&slot->sl_mutex);
	c2_rpc_slot_reply_received(reply->ri_slot_refs[0].sr_slot,
				   reply, &tmp);
	c2_mutex_unlock(&slot->sl_mutex);
        reply->ri_mach = reply->ri_session->s_conn->c_rpcmachine;
        request->ri_mach = request->ri_session->s_conn->c_rpcmachine;
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
	c2_rpc_session_module_init();
	c2_rpc_form_init();
	return 0;
}

void c2_rpc_core_fini(void)
{
	c2_rpc_session_module_fini();
}

int c2_rpcmachine_src_ep_add(struct c2_rpcmachine *machine,
		struct c2_net_end_point *src_ep)
{
	struct c2_rpc_chan		*chan = NULL;
	struct c2_clink			 tmwait;
	int				 rc = 0;
	int				 st = 0;

	C2_PRE(machine != NULL);
	C2_PRE(src_ep != NULL);

	/* Create a new chan structure, initialize a transfer machine. */
	rc = c2_rpc_chan_create(&chan, machine, src_ep);
	if (rc < 0) {
		return rc;
	}

	/* Start the transfer machine so that users of this rpcmachine
	   can send/receive messages. */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&chan->rc_xfermc.ntm_chan, &tmwait);

	rc = c2_net_tm_start(&chan->rc_xfermc, src_ep);
	if (rc < 0) {
		c2_rpc_chan_destroy(machine, chan);
		c2_clink_del(&tmwait);
		return rc;
	}

	/* Wait on transfer machine channel till transfer machine is
	   actually started. */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);

	/* If tm fails to start, propogate the error back. */
	if (chan->rc_xfermc.ntm_state != C2_NET_TM_STARTED) {
		c2_rpc_chan_destroy(machine, chan);
		return rc;
	}

	/* Add buffers for receiving messages to this transfer machine. */
	rc = c2_rpc_net_recv_buffer_allocate_nr(src_ep->nep_dom,
			&chan->rc_xfermc);
	if (rc < 0) {
		st = c2_net_tm_stop(&chan->rc_xfermc, true);
		c2_rpc_chan_destroy(machine, chan);
		return rc;
	}

	return rc;
}

void c2_rpc_chan_ref_release(struct c2_ref *ref)
{
	int				 rc = 0;
	struct c2_rpc_chan		*chan = NULL;
	struct c2_clink			 tmwait;

	C2_PRE(ref != NULL);

	chan = container_of(ref, struct c2_rpc_chan, rc_ref);
	C2_ASSERT(chan != NULL);

	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&chan->rc_xfermc.ntm_chan, &tmwait);

	/* Stop the transfer machine first. */
	rc = c2_net_tm_stop(&chan->rc_xfermc, false);
	if (rc < 0) {
		/* XXX Post an addb event here. */
		return;
	}

	/* Wait for transfer machine to stop. */
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);

	/* Delete all the buffers from transfer machine and fini the
	   transfer machine. */
	c2_rpc_net_recv_buffer_deallocate_nr(&chan->rc_xfermc,
			chan->rc_xfermc.ntm_dom);

	/* Destroy the chan structure. */
	c2_rpc_chan_destroy(chan->rc_rpcmachine, chan);
}

int c2_rpc_chan_create(struct c2_rpc_chan **chan, struct c2_rpcmachine *machine,
		struct c2_net_end_point *ep)
{
	int			 rc = 0;
	struct c2_rpc_chan	*ch = NULL;

	C2_PRE(chan != NULL);
	C2_PRE(ep != NULL);

	/* Allocate new rpc chan.*/
	C2_ALLOC_PTR(ch);
	if (ch == NULL)
		return -ENOMEM;
	c2_ref_init(&ch->rc_ref, 1, c2_rpc_chan_ref_release);

	ch->rc_xfermc.ntm_state = C2_NET_TM_UNDEFINED;
	ch->rc_xfermc.ntm_callbacks = &c2_rpc_tm_callbacks;
	ch->rc_rpcmachine = machine;
	rc = c2_net_tm_init(&ch->rc_xfermc, ep->nep_dom);
	if (rc < 0) {
		c2_free(ch);
		return rc;
	}

	/* Add the new rpc chan structure to list of such structures in
	   rpcmachine.*/
	c2_mutex_lock(&machine->cr_ep_aggr.ea_mutex);
	/* By default, all new channels are added at the end of list.
	   At the head, there is a c2_rpc_chan, which was added by
	   c2_rpcmachine_init and its refcount needs to be decremented
	   during c2_rpcmachine_fini, hence this arrangement. */
	c2_list_add_tail(&machine->cr_ep_aggr.ea_chan_list, &ch->rc_linkage);
	c2_mutex_unlock(&machine->cr_ep_aggr.ea_mutex);
	*chan = ch;

	return rc;
}

struct c2_rpc_chan *c2_rpc_chan_get(struct c2_rpcmachine *machine)
{
	struct c2_rpc_chan	*chan = NULL;
	struct c2_rpc_chan	*chan_found = NULL;
	struct c2_atomic64	 ref;

	C2_PRE(machine != NULL);

	c2_atomic64_set(&ref, 0);
	/* The current policy is to return a c2_rpc_chan structure
	   with least refcount. This can be enhanced later to take
	   into account multiple parameters. */
	c2_mutex_lock(&machine->cr_ep_aggr.ea_mutex);
	c2_list_for_each_entry(&machine->cr_ep_aggr.ea_chan_list, chan,
			struct c2_rpc_chan, rc_linkage) {
		if (c2_atomic64_get(&chan->rc_ref.ref_cnt) <=
				c2_atomic64_get(&ref)) {
			chan_found = chan;
		}
	}
	if (chan_found == NULL) {
		chan_found = c2_list_entry(c2_list_first(&machine->
					cr_ep_aggr.ea_chan_list),
				struct c2_rpc_chan, rc_linkage);
	}
	C2_ASSERT(chan_found != NULL);
	c2_ref_get(&chan_found->rc_ref);
	c2_mutex_unlock(&machine->cr_ep_aggr.ea_mutex);
	return chan_found;
}

void c2_rpc_chan_put(struct c2_rpc_chan *chan)
{
	C2_PRE(chan != NULL);

	c2_ref_put(&chan->rc_ref);
}

void c2_rpc_chan_destroy(struct c2_rpcmachine *machine,
		struct c2_rpc_chan *chan)
{
	C2_PRE(chan != NULL);

	/* Remove chan from list of such structures in rpcmachine. */
	c2_mutex_lock(&machine->cr_ep_aggr.ea_mutex);
	c2_list_del(&chan->rc_linkage);
	c2_mutex_unlock(&machine->cr_ep_aggr.ea_mutex);

	/* Fini the transfer machine here and deallocate the chan. */
	c2_net_tm_fini(&chan->rc_xfermc);
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

static int rpc_stat_init(struct c2_rpc_statistics *stat)
{
	return 0;
}

static void rpc_stat_fini(struct c2_rpc_statistics *stat)
{
}

/**
   The callback routine to be called once the transfer machine
   receives a buffer. This subroutine later invokes decoding of
   net buffer and then notifies sessions component about every
   incoming rpc item.
 */
static void c2_rpc_net_buf_received(const struct c2_net_buffer_event *ev)
{
	struct c2_rpc		 rpc;
	struct c2_rpc_item	*item = NULL;
	struct c2_net_buffer	*nb = NULL;
	struct c2_rpc_chan	*chan = NULL;
	int			 rc = 0;
	int			 i = 0;
	c2_time_t		 now;

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
			c2_list_for_each_entry(&rpc.r_items, item,
					struct c2_rpc_item,
					ri_rpcobject_linkage) {
				/* If this is a reply type rpc item, call a
				   sessions/slots method on it which will find
				   out its corresponding request item and call
				   its completion callback.*/
				chan = container_of(nb->nb_tm,
						struct c2_rpc_chan, rc_xfermc);
				item->ri_mach = chan->rc_rpcmachine;
				nb->nb_ep = ev->nbe_ep;
				item->ri_src_ep = nb->nb_ep;
				printf("item->src_ep = %p\n", item->ri_src_ep);
				printf("Item %d received\n", i);
				item->ri_rpc_entry_time = now;
				c2_rpc_item_attach(item);
				rc = c2_rpc_item_received(item);
				if (rc == 0) {
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
	nb->nb_ep = NULL;
	rc = c2_net_buffer_add(nb, nb->nb_tm);
	C2_ASSERT(rc == 0);
}

static struct c2_net_buffer *c2_rpc_net_buffer_allocate(
		struct c2_net_domain *net_dom, struct c2_net_buffer *nbuf,
		enum c2_net_queue_type qtype)
{
	uint32_t			 rc = 0;
	struct c2_net_buffer		*nb = NULL;
	int32_t				 nr_segs = 0;
	c2_bcount_t			 seg_size = 0;
	c2_bcount_t			 buf_size = 0;
	c2_bcount_t			 nrsegs = 0;

	C2_PRE(net_dom != NULL);
	C2_PRE((qtype == C2_NET_QT_MSG_RECV) || (qtype = C2_NET_QT_MSG_SEND));

	if (nbuf == NULL) {
		C2_ALLOC_PTR(nb);
		if (nb == NULL) {
			return nb;
		}
	} else {
		nb = nbuf;
	}
	buf_size = c2_net_domain_get_max_buffer_size(net_dom);
	nr_segs = c2_net_domain_get_max_buffer_segments(net_dom);
	seg_size = c2_net_domain_get_max_buffer_segment_size(net_dom);

	/* Allocate the bufvec of size = min((buf_size), (nr_segs * seg_size)).
	   We keep the segment size constant. So mostly the number of segments
	   is changed here. */
	if (buf_size > (nr_segs * seg_size)) {
		nrsegs = nr_segs;
	} else {
		nrsegs = buf_size / seg_size;
	}
	rc = c2_bufvec_alloc(&nb->nb_buffer, nrsegs, seg_size);
	if (rc < 0) {
		if (nbuf == NULL) {
			c2_free(nb);
		}
		return NULL;
	}

	nb->nb_flags = 0;
	nb->nb_qtype = qtype;
	if (qtype == C2_NET_QT_MSG_RECV) {
		nb->nb_callbacks = &c2_rpc_rcv_buf_callbacks;
	} else {
		nb->nb_callbacks = &c2_rpc_send_buf_callbacks;
	}

	/* Register the buffer with given net domain. */
	rc = c2_net_buffer_register(nb, net_dom);
	if (rc < 0) {
		c2_bufvec_free(&nb->nb_buffer);
		if (nbuf == NULL) {
			c2_free(nb);
		}
	}
	return nb;
}

struct c2_net_buffer *c2_rpc_net_recv_buffer_allocate(
		struct c2_net_domain *net_dom)
{
	return c2_rpc_net_buffer_allocate(net_dom, NULL, C2_NET_QT_MSG_RECV);
}

void c2_rpc_net_send_buffer_allocate(
		struct c2_net_domain *net_dom, struct c2_net_buffer *nb)
{
	struct c2_net_buffer	*nbuf = NULL;
	nbuf = c2_rpc_net_buffer_allocate(net_dom, nb, C2_NET_QT_MSG_SEND);
}

int c2_rpc_net_recv_buffer_allocate_nr(struct c2_net_domain *net_dom,
		struct c2_net_transfer_mc *tm)
{
	int			 rc = 0;
	int			 st = 0;
	uint32_t		 i = 0;
	struct c2_net_buffer	*nb = NULL;

	C2_PRE(net_dom != NULL);

	for (i = 0; i < C2_RPC_TM_RECV_BUFFERS_NR; ++i) {
		nb = c2_rpc_net_recv_buffer_allocate(net_dom);
		if (nb == NULL) {
			rc = -ENOMEM;
			break;
		}
		rc = c2_net_buffer_add(nb, tm);
		if (rc < 0) {
			break;
		}
	}
	if (rc < 0) {
		st = c2_rpc_net_recv_buffer_deallocate_nr(tm, net_dom);
		if (st < 0)
			return rc;
		c2_net_tm_fini(tm);
	}
	return rc;
}

int c2_rpc_net_recv_buffer_deallocate(struct c2_net_buffer *nb,
		struct c2_net_transfer_mc *tm, struct c2_net_domain *net_dom)
{
	int			rc = 0;
	struct c2_clink		tmwait;

	C2_PRE(nb != NULL);
	C2_PRE(tm != NULL);
	C2_PRE(net_dom != NULL);

	/* Add to a clink to transfer machine's channel to wait for
	   deletion of buffers from transfer machine. */
	c2_clink_init(&tmwait, NULL);
	c2_clink_add(&tm->ntm_chan, &tmwait);

	c2_net_buffer_del(nb, tm);
	c2_chan_wait(&tmwait);
	c2_clink_del(&tmwait);

	rc = c2_net_buffer_deregister(nb, net_dom);
	if (rc < 0) {
		/* XXX Post an addb event.*/
	}

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

	rc = c2_net_buffer_deregister(nb, net_dom);
	if (rc < 0) {
		return rc;
	}
	c2_bufvec_free(&nb->nb_buffer);
	return rc;
}

int c2_rpc_net_recv_buffer_deallocate_nr(struct c2_net_transfer_mc *tm,
		struct c2_net_domain *net_dom)
{
	int			 rc = 0;
	struct c2_net_buffer	*nb = NULL;
	struct c2_net_buffer	*nb_next = NULL;

	C2_PRE(tm != NULL);
	C2_PRE(net_dom != NULL);

	c2_list_for_each_entry_safe(&tm->ntm_q[C2_NET_QT_MSG_RECV], nb,
			nb_next, struct c2_net_buffer, nb_tm_linkage) {
		c2_net_buffer_del(nb, tm);
		rc = c2_rpc_net_recv_buffer_deallocate(nb, tm, net_dom);
		if (rc < 0) {
			/* XXX Post an addb event here. */
			break;
		}
	}
	return rc;
}

int c2_rpcmachine_init(struct c2_rpcmachine	*machine,
		struct c2_cob_domain	*dom,
		struct c2_net_end_point	*src_ep)
{
	struct c2_db_tx			 tx;
	struct c2_cob			*root_session_cob;
	struct c2_net_domain		*net_dom;
	int				 rc;
	struct c2_rpc_stats		*stats;

	/* The c2_net_domain is expected to be created by end user.*/
	C2_PRE(machine != NULL);
	C2_PRE(dom != NULL);
	C2_PRE(src_ep != NULL);

	C2_ALLOC_PTR(stats);
	if (stats == NULL)
		return -ENOMEM;

	machine->cr_rpc_stats = stats;

	rc = rpc_proc_init(&machine->cr_processing);
	if (rc < 0)
		return rc;

	rc = rpc_stat_init(&machine->cr_statistics);
	if (rc < 0) {
		rpc_proc_fini(&machine->cr_processing);
	}

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
        net_dom = src_ep->nep_dom;
	C2_ASSERT(net_dom != NULL);
	c2_rpc_ep_aggr_init(&machine->cr_ep_aggr);

	rc = c2_rpcmachine_src_ep_add(machine, src_ep);
	return rc;
}

void c2_rpcmachine_fini(struct c2_rpcmachine *machine)
{
	struct c2_rpc_chan	*chan = NULL;

	C2_PRE(machine != NULL);

	rpc_stat_fini(&machine->cr_statistics);
	rpc_proc_fini(&machine->cr_processing);
	/* XXX commented following two lines for testing purpose */
	//c2_list_fini(&machine->cr_incoming_conns);
	//c2_list_fini(&machine->cr_outgoing_conns);
	//c2_list_fini(&machine->cr_rpc_conn_list);
	c2_list_fini(&machine->cr_ready_slots);
	c2_mutex_fini(&machine->cr_session_mutex);

	/* Release the reference on the source endpoint and the
	   concerned c2_rpc_chan here.
	   The chan structure at head of list is the one that was added
	   during rpcmachine_init and its reference has to be released here. */
	chan = c2_list_entry(c2_list_first(&machine->cr_ep_aggr.ea_chan_list),
			struct c2_rpc_chan, rc_linkage);
	c2_ref_put(&chan->rc_ref);
	c2_rpc_ep_aggr_fini(&machine->cr_ep_aggr);
	c2_free(&machine->cr_rpc_stats);
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
	.rio_sent    = rpc_item_op_sent,
	.rio_added   = rpc_item_op_added,
	.rio_replied = rpc_item_op_replied
};

/** simple vector of update stream operations */
void us_timeout(struct c2_update_stream *us)
{
	DBG("us: ssid: %lu, slotid: %lu, TIMEOUT\n", us->us_session_id, us->us_slot_id);
}
void us_recovery_complete(struct c2_update_stream *us)
{
	DBG("us: ssid: %lu, slotid: %lu, RECOVERED\n", us->us_session_id, us->us_slot_id);
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
	fop->f_type->ft_ops->fto_fop_replied(fop);
}

/**
   RPC item ops function
   Function to return size of fop
 */
uint64_t c2_rpc_item_size(struct c2_rpc_item *item)
{
	struct c2_fop			*fop = NULL;
	uint64_t			 size = 0;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	if(fop->f_type->ft_ops->fto_getsize) {
		size = fop->f_type->ft_ops->fto_getsize(fop);
	} else {
		size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	}

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

/**
   Return opcode of the fop referenced by given rpc item.
 */
int c2_rpc_item_get_opcode(struct c2_rpc_item *item)
{
	struct c2_fop		*fop = NULL;
	int			 opcode = 0;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	opcode = fop->f_type->ft_ops->fto_get_opcode(fop);
	return opcode;
}


/**
   RPC item ops function
   Function to get the fid for an IO request from the rpc item
 */
struct c2_fid c2_rpc_item_io_get_fid(struct c2_rpc_item *item)
{
	struct c2_fop			*fop = NULL;
	struct c2_fid			 fid;
	struct c2_fop_file_fid		 ffid;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);

	ffid = fop->f_type->ft_ops->fto_get_fid(fop);
	c2_rpc_form_item_io_fid_wire2mem(&ffid, &fid);
	return fid;
}

/**
   RPC item ops function
   Function to find out if the item belongs to an IO request or not
 */
bool c2_rpc_item_is_io_req(struct c2_rpc_item *item)
{
	struct c2_fop		*fop = NULL;
	bool			 io_req = false;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	io_req = fop->f_type->ft_ops->fto_is_io(fop);
	return io_req;
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

int c2_rpc_item_io_coalesce(void *c_item, struct c2_rpc_item *b_item);

const struct c2_rpc_item_type_ops c2_rpc_item_readv_type_ops = {
	.rio_sent = NULL,
	.rio_added = NULL,
	.rio_replied = c2_rpc_item_replied,
	.rio_item_size = c2_rpc_item_size,
	.rio_items_equal = c2_rpc_item_equal,
	.rio_io_get_opcode = c2_rpc_item_get_opcode,
	.rio_io_get_fid = c2_rpc_item_io_get_fid,
	.rio_is_io_req = c2_rpc_item_is_io_req,
	.rio_get_io_fragment_count = c2_rpc_item_get_io_fragment_count,
	.rio_io_coalesce = c2_rpc_item_io_coalesce,
};

const struct c2_rpc_item_type_ops c2_rpc_item_writev_type_ops = {
	.rio_sent = NULL,
	.rio_added = NULL,
	.rio_replied = c2_rpc_item_replied,
	.rio_item_size = c2_rpc_item_size,
	.rio_items_equal = c2_rpc_item_equal,
	.rio_io_get_opcode = c2_rpc_item_get_opcode,
	.rio_io_get_fid = c2_rpc_item_io_get_fid,
	.rio_is_io_req = c2_rpc_item_is_io_req,
	.rio_get_io_fragment_count = c2_rpc_item_get_io_fragment_count,
	.rio_io_coalesce = c2_rpc_item_io_coalesce,
};

const struct c2_rpc_item_type_ops c2_rpc_item_create_type_ops = {
	.rio_sent = NULL,
	.rio_added = NULL,
	.rio_replied = c2_rpc_item_replied,
	.rio_item_size = c2_rpc_item_default_size,
	.rio_items_equal = c2_rpc_item_equal,
	.rio_io_get_opcode = c2_rpc_item_get_opcode,
	.rio_io_get_fid = c2_rpc_item_io_get_fid,
	.rio_is_io_req = c2_rpc_item_is_io_req,
	.rio_get_io_fragment_count = NULL,
	.rio_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

const struct c2_rpc_item_type_ops c2_rpc_item_create_rep_type_ops = {
        .rio_sent = NULL,
        .rio_added = NULL,
        .rio_replied = c2_rpc_item_replied,
        .rio_item_size = c2_rpc_item_default_size,
        .rio_items_equal = c2_rpc_item_equal,
        .rio_io_get_opcode = c2_rpc_item_get_opcode,
        .rio_io_get_fid = c2_rpc_item_io_get_fid,
        .rio_is_io_req = c2_rpc_item_is_io_req,
        .rio_get_io_fragment_count = NULL,
        .rio_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

struct c2_rpc_item_type c2_rpc_item_type_readv = {
	.rit_ops = &c2_rpc_item_readv_type_ops,
};

struct c2_rpc_item_type c2_rpc_item_type_writev = {
	.rit_ops = &c2_rpc_item_writev_type_ops,
};

struct c2_rpc_item_type c2_rpc_item_type_create = {
	.rit_ops = &c2_rpc_item_create_type_ops,
	.rit_mutabo = true,
	.rit_item_is_req = true,
};

struct c2_rpc_item_type c2_rpc_item_type_create_rep = {
	.rit_ops = &c2_rpc_item_create_rep_type_ops,
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
                case c2_io_service_readv_opcode:
                        item->ri_type = &c2_rpc_item_type_readv;
                        break;
                case c2_io_service_writev_opcode:
                        item->ri_type = &c2_rpc_item_type_writev;
                        break;
                case c2_io_service_create_opcode:
                        item->ri_type = &c2_rpc_item_type_create;
                        break;
                case c2_io_service_create_rep_opcode:
                        item->ri_type = &c2_rpc_item_type_create_rep;
                        break;
                default:
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
		case c2_io_service_readv_opcode:
			fopt->ft_ritype = &c2_rpc_item_type_readv;
			break;
		case c2_io_service_writev_opcode:
			fopt->ft_ritype = &c2_rpc_item_type_writev;
			break;
		case c2_io_service_create_opcode:
			fopt->ft_ritype = &c2_rpc_item_type_create;
			break;
		default:
			break;
	};
}

/** Set the incoming exit stats for an rpc item */
void c2_rpc_item_set_incoming_exit_stats(struct c2_rpc_item *item)
{
	c2_time_t                now;
	struct c2_rpc_stats	*stats;
	
	C2_PRE(item != NULL);

	c2_time_now(&now);
	item->ri_rpc_exit_time = now;

	stats = item->ri_mach->cr_rpc_stats;
	stats->rs_in_instant_latency = c2_time_sub(item->ri_rpc_exit_time,
			item->ri_rpc_entry_time);
	if ((stats->rs_in_min_latency >= stats->rs_in_instant_latency) ||
			(stats->rs_in_min_latency == 0))
		stats->rs_in_min_latency = stats->rs_in_instant_latency;
	if ((stats->rs_in_max_latency <= stats->rs_in_instant_latency) ||
		(stats->rs_in_max_latency == 0))
		stats->rs_in_max_latency = stats->rs_in_instant_latency;
	stats->rs_in_avg_latency = ((stats->rs_num_in_items *
				stats->rs_in_avg_latency) +
			stats->rs_in_instant_latency) /
		(stats->rs_num_in_items +1);
	stats->rs_num_in_items++;
	stats->rs_num_in_bytes += c2_rpc_item_default_size(item);	
}

/** Set the outgoing exit stats for an rpc item */
void c2_rpc_item_set_outgoing_exit_stats(struct c2_rpc_item *item)
{
	c2_time_t                now;
	struct c2_rpc_stats	*stats;
	
	C2_PRE(item != NULL);

	c2_time_now(&now);
	item->ri_rpc_exit_time = now;

	stats = item->ri_mach->cr_rpc_stats;
	stats->rs_out_instant_latency = c2_time_sub(item->ri_rpc_exit_time,
			item->ri_rpc_entry_time);
	if ((stats->rs_out_min_latency >= stats->rs_out_instant_latency) ||
			(stats->rs_out_min_latency == 0))
		stats->rs_out_min_latency = stats->rs_out_instant_latency;
	if ((stats->rs_out_max_latency <= stats->rs_out_instant_latency) ||
		(stats->rs_out_max_latency == 0))
		stats->rs_out_max_latency = stats->rs_out_instant_latency;
	stats->rs_out_avg_latency = ((stats->rs_num_out_items *
				stats->rs_out_avg_latency) +
			stats->rs_out_instant_latency) /
		(stats->rs_num_out_items +1);
	stats->rs_num_out_items++;
	stats->rs_num_out_bytes += c2_rpc_item_default_size(item);	
}

/* Dummy reqh queue of items */

struct c2_queue          exec_queue;
struct c2_chan		exec_chan;

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
