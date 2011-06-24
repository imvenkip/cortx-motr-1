#include "cob/cob.h"
#include "rpc/rpccore.h"
#include "rpc/rpcdbg.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "rpc/session.h"
#include "rpc/session_int.h"
#include "fop/fop.h"
#include "formation.h"

#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

/**
   Transfer machine callback vector for transfer machines created by
   rpc layer.
 */
struct c2_net_tm_callbacks c2_rpc_tm_callbacks = {
	.ntc_event_cb = c2_rpc_tm_event_cb
};

static const struct c2_update_stream_ops update_stream_ops;
static const struct c2_rpc_item_type_ops rpc_item_ops;

struct c2_rpc_ep_aggr		*rpc_ep_aggr = NULL;

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

int c2_rpc_item_init(struct c2_rpc_item *item,
		     struct c2_rpcmachine *mach)
{
	c2_ref_init(&item->ri_ref, 1, c2_rpc_item_ref_fini);
	c2_chan_init(&item->ri_chan);
	item->ri_state = RPC_ITEM_UNINITIALIZED;
	item->ri_type = NULL;
	//item->ri_sender_id = SENDER_ID_INVALID;
	//item->ri_session_id = SESSION_ID_INVALID;
	//item->ri_slot_id = SLOT_ID_INVALID;
	item->ri_mach = mach;
	return 0;
}
int c2_rpc_post(struct c2_rpc_item	*item)
{
	return 0;
}
int c2_rpc_reply_post(struct c2_rpc_item	*request,
		      struct c2_rpc_item	*reply)
{
	return 0;
}
bool c2_rpc_item_is_update(struct c2_rpc_item *item)
{
	return (item->ri_flags & RPC_ITEM_MUTABO) != 0;
}

void c2_rpc_tm_event_cb(const struct c2_net_tm_event *ev)
{
}

int c2_rpc_ep_aggr_create()
{
	C2_ALLOC_PTR(rpc_ep_aggr);
	if (!rpc_ep_aggr)
		return -ENOMEM;
	return 0;
}

void c2_rpc_ep_aggr_destroy()
{
	/* By this time, all elements of list should have been finied
	   and the list should be empty.*/
	c2_list_fini(&rpc_ep_aggr->ea_chan_list);
	c2_free(rpc_ep_aggr);
	rpc_ep_aggr = NULL;
}

int  c2_rpc_core_init(void)
{
	int rc = 0;

	rc = c2_rpc_ep_aggr_create();
	if (!rc)
		return rc;
	return 0;
}

void c2_rpc_core_fini(void)
{
	c2_rpc_ep_aggr_destroy();
}

int c2_rpc_chan_create(struct c2_rpc_chan **chan, struct c2_net_end_point
		*ep, struct c2_net_transfer_mc *tm)
{
	int			 rc = 0;
	struct c2_rpc_chan	*ch = NULL;

	C2_PRE(chan != NULL);
	C2_PRE(ep != NULL);
	C2_PRE(tm != NULL);

	C2_ALLOC_PTR(ch);
	if (!ch)
		return -ENOMEM;
	c2_list_link_init(&ch->rc_linkage);
	ch->rc_endp = ep;
	ch->rc_xfermc = tm;
	c2_mutex_lock(&rpc_ep_aggr->ea_mutex);
	c2_list_add(&rpc_ep_aggr->ea_chan_list, &ch->rc_linkage);
	*chan = ch;
	return rc;
}

struct c2_rpc_chan *c2_rpc_chan_locate(struct c2_net_end_point *ep)
{
	struct c2_rpc_chan	*chan = NULL;
	bool			 found = false;

	C2_PRE(ep != NULL);

	c2_mutex_lock(&rpc_ep_aggr->ea_mutex);
	c2_list_for_each_entry(&rpc_ep_aggr->ea_chan_list, chan,
			struct c2_rpc_chan, rc_linkage) {
		if (chan->rc_endp == ep) {
			found = true;
			break;
		}
	}
	c2_mutex_unlock(&rpc_ep_aggr->ea_mutex);
	if (!found) {
		chan = NULL;
	}
	return chan;
}

void c2_rpc_chan_destroy(struct c2_rpc_chan *chan)
{
	C2_PRE(chan != NULL);
	c2_list_del(&chan->rc_linkage);
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

int c2_rpc_decode(struct c2_net_buffer *nb, struct c2_rpc *rpcobj)
{
	return 0;
}

void c2_rpc_reply_received(const struct c2_net_buffer_event *ev)
{
	struct c2_rpc		 rpc;
	struct c2_rpc_item	*item = NULL;
	struct c2_net_buffer	*nb = NULL;
	int			 rc = 0;

	C2_PRE(ev != NULL);

	/* Decode the buffer, get an RPC from it, traverse the
	   list of rpc items from that rpc and post reply callbacks
	   for each rpc item. */
	nb = ev->nbe_buffer;
	c2_list_link_init(&rpc.r_linkage);
	c2_list_init(&rpc.r_items);
	rc = c2_rpc_decode(nb, &rpc);
	if (!rc) {
		/* XXX We can post an ADDB event here. */
	}

	c2_list_for_each_entry(&rpc.r_items, item, struct c2_rpc_item,
			ri_rpcobject_linkage) {
		/* If this is a reply type rpc item, call a sessions/slots
		   method on it which will find out its corresponding
		   request item and call its completion callback.*/
		if (!item_is_request(item)) {
			rc = c2_rpc_item_received(item);
			if (!rc) {
				/* Post an ADDB event here.*/
			}
		}
	}

	/* Add the c2_net_buffer back to the queue of transfer machine. */
	rc = c2_net_buffer_add(nb, nb->nb_tm);
}

/**
   Buffer callback for buffers added by rpc layer for receiving messages.
 */
struct c2_net_buffer_callbacks c2_rpc_rcv_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_RECV]	= c2_rpc_reply_received,
	}
};

struct c2_net_buffer *c2_rpc_net_buffer_create(struct c2_net_domain *net_dom)
{
	uint32_t			 rc = 0;
	struct c2_net_buffer		*nb = NULL;
	uint32_t			 nr_segs = 0;
	c2_bcount_t			 seg_size = 0;

	C2_PRE(net_dom != NULL);

	C2_ALLOC_PTR(nb);
	if (!nb) {
		return nb;
	}
	nr_segs = c2_net_domain_get_max_buffer_segments(net_dom);
	seg_size = c2_net_domain_get_max_buffer_segment_size(net_dom);
	rc = c2_bufvec_alloc(&nb->nb_buffer, nr_segs, seg_size);
	if (!rc)
		return NULL;

	nb->nb_flags = 0;
	nb->nb_qtype = C2_NET_QT_MSG_RECV;
	nb->nb_callbacks = &c2_rpc_rcv_buf_callbacks;

	return nb;
}

int c2_rpcmachine_init(struct c2_rpcmachine	*machine,
			struct c2_cob_domain	*dom)
{
	struct c2_db_tx			 tx;
	struct c2_cob			*root_session_cob;
	int rc;

	/* The c2_net_domain is expected to be created by end user.*/
	C2_PRE(machine != NULL);
	C2_PRE(dom != NULL);

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

	return rc;
}

void c2_rpcmachine_fini(struct c2_rpcmachine *machine)
{
	rpc_stat_fini(&machine->cr_statistics);
	rpc_proc_fini(&machine->cr_processing);	
	/* XXX commented following two lines for testing purpose */
	//c2_list_fini(&machine->cr_incoming_conns);
	//c2_list_fini(&machine->cr_outgoing_conns);
	//c2_list_fini(&machine->cr_rpc_conn_list);
	c2_list_fini(&machine->cr_ready_slots);
	c2_mutex_fini(&machine->cr_session_mutex);
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
	size = fop->f_type->ft_ops->fto_getsize(fop);
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
	.rio_item_size = c2_rpc_item_size,
	.rio_items_equal = c2_rpc_item_equal,
	.rio_io_get_opcode = c2_rpc_item_get_opcode,
	.rio_io_get_fid = c2_rpc_item_io_get_fid,
	.rio_is_io_req = c2_rpc_item_is_io_req,
	.rio_get_io_fragment_count = NULL,
	.rio_io_coalesce = NULL,
};

struct c2_rpc_item_type c2_rpc_item_type_readv = {
	.rit_ops = &c2_rpc_item_readv_type_ops,
};

struct c2_rpc_item_type c2_rpc_item_type_writev = {
	.rit_ops = &c2_rpc_item_writev_type_ops,
};

struct c2_rpc_item_type c2_rpc_item_type_create = {
	.rit_ops = &c2_rpc_item_create_type_ops,
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

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
