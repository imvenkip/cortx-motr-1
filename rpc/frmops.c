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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 06/04/2012
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/errno.h"
#include "lib/arith.h"
#include "lib/bob.h"
#include "lib/finject.h"

#include "colibri/magic.h"
#include "net/net.h"

#include "rpc/formation2.h"
#include "rpc/packet.h"
#include "rpc/rpc2.h"
#include "rpc/rpc_machine.h"
#include "rpc/item.h"
#include "rpc/session_internal.h"

static bool packet_ready(struct c2_rpc_packet *p);
static bool item_bind(struct c2_rpc_item *item);

static int net_buffer_allocate(struct c2_net_buffer *netbuf,
			       struct c2_net_domain *ndom,
			       c2_bcount_t           buf_size);

static void net_buffer_free(struct c2_net_buffer *netbuf,
			    struct c2_net_domain *ndom);

static void bufvec_geometry(struct c2_net_domain *ndom,
			    c2_bcount_t           buf_size,
			    int32_t              *out_nr_segments,
			    c2_bcount_t          *out_segment_size);

static void item_done(struct c2_rpc_item *item, unsigned long rc);

/*
 * This is the only symbol exported from this file.
 */
const struct c2_rpc_frm_ops c2_rpc_frm_default_ops = {
	.fo_packet_ready = packet_ready,
	.fo_item_bind    = item_bind,
};

/**
   RPC layer's wrapper on c2_net_buffer. rpc_buffer carries one packet at
   a time.

   NOTE: rpc_buffer and packet are kept separate, even though their
         lifetime is same at this point.
         A buffer can carry only one packet at some point. There is a limit
         on number of packets that can be "in-flight". In near future we
         might want to keep per c2_rpc_chan buffer pool with few pre-allocated
         buffers. In that case same buffer will carry many packets  during
         its lifetime (but only one at a time).
 */
struct rpc_buffer {
	struct c2_net_buffer   rb_netbuf;
	struct c2_rpc_packet  *rb_packet;
	/** see C2_RPC_BUF_MAGIC */
	uint64_t               rb_magic;
};

static const struct c2_bob_type rpc_buffer_bob_type = {
	.bt_name         = "rpc_buffer",
	.bt_magix_offset = C2_MAGIX_OFFSET(struct rpc_buffer, rb_magic),
	.bt_magix        = C2_RPC_BUF_MAGIC,
	.bt_check        = NULL,
};

C2_BOB_DEFINE(static, &rpc_buffer_bob_type, rpc_buffer);

static int rpc_buffer_init(struct rpc_buffer    *rpcbuf,
			   struct c2_rpc_packet *p);

static int rpc_buffer_submit(struct rpc_buffer *rpcbuf);

static void rpc_buffer_fini(struct rpc_buffer *rpcbuf);

static void outgoing_buf_event_handler(const struct c2_net_buffer_event *ev);

static const struct c2_net_buffer_callbacks outgoing_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_SEND] = outgoing_buf_event_handler
	}
};

static struct c2_rpc_machine *
rpc_buffer__rmachine(const struct rpc_buffer *rpcbuf)
{
	struct c2_rpc_machine *rmachine;

	C2_PRE(rpc_buffer_bob_check(rpcbuf) &&
	       rpcbuf->rb_packet != NULL &&
	       rpcbuf->rb_packet->rp_frm != NULL);

	rmachine = frm_rmachine(rpcbuf->rb_packet->rp_frm);
	C2_ASSERT(rmachine != NULL);

	return rmachine;
}

/**
   Serialises packet p and its items in a network buffer and submits it to
   network layer.

   @see c2_rpc_frm_ops::fo_packet_ready()
 */
static bool packet_ready(struct c2_rpc_packet *p)
{
	struct rpc_buffer *rpcbuf;
	int                rc;

	C2_ENTRY("packet: %p", p);
	C2_PRE(c2_rpc_packet_invariant(p));

	C2_ALLOC_PTR_ADDB(rpcbuf, &c2_rpc_addb_ctx, &c2_rpc_addb_loc);
	if (rpcbuf == NULL) {
		rc = -ENOMEM;
		C2_LOG(C2_ERROR, "Failed to allocate rpcbuf");
		goto out;
	}
	rc = rpc_buffer_init(rpcbuf, p);
	if (rc != 0)
		goto out_free;

	rc = rpc_buffer_submit(rpcbuf);
	if (rc != 0)
		goto out_fini;

	C2_LEAVE("true");
	return true;

out_fini:
	rpc_buffer_fini(rpcbuf);

out_free:
	C2_ASSERT(rpcbuf != NULL);
	c2_free(rpcbuf);

out:
	c2_rpc_packet_traverse_items(p, item_done, rc);
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);

	C2_LEAVE("false");
	return false;
}

/**
   Initialises rpcbuf, allocates network buffer of size enough to
   accomodate serialised packet p.
 */
static int rpc_buffer_init(struct rpc_buffer    *rpcbuf,
			   struct c2_rpc_packet *p)
{
	struct c2_net_buffer  *netbuf;
	struct c2_net_domain  *ndom;
	struct c2_rpc_machine *machine;
	struct c2_rpc_chan    *rchan;
	int                   rc;

	C2_ENTRY("rbuf: %p packet: %p", rpcbuf, p);
	C2_PRE(rpcbuf != NULL && p != NULL);

	machine = frm_rmachine(p->rp_frm);
	ndom    = machine->rm_tm.ntm_dom;
	C2_ASSERT(ndom != NULL);

	netbuf = &rpcbuf->rb_netbuf;
	rc = net_buffer_allocate(netbuf, ndom, p->rp_size);
	if (rc != 0)
		goto out;

	rc = c2_rpc_packet_encode(p, &netbuf->nb_buffer);
	if (rc != 0) {
		net_buffer_free(netbuf, ndom);
		goto out;
	}
	rchan = frm_rchan(p->rp_frm);
	netbuf->nb_length = c2_vec_count(&netbuf->nb_buffer.ov_vec);
	netbuf->nb_ep     = rchan->rc_destep;

	rpcbuf->rb_packet = p;
	rpc_buffer_bob_init(rpcbuf);

out:
	C2_RETURN(rc);
}

/**
   Allocates network buffer and register it with network domain ndom.
 */
static int net_buffer_allocate(struct c2_net_buffer *netbuf,
			       struct c2_net_domain *ndom,
			       c2_bcount_t           buf_size)
{
	c2_bcount_t segment_size;
	int32_t     nr_segments;
	int         rc;

	C2_ENTRY("netbuf: %p ndom: %p bufsize: %llu", netbuf, ndom,
						 (unsigned long long)buf_size);
	C2_PRE(netbuf != NULL && ndom != NULL && buf_size > 0);

	bufvec_geometry(ndom, buf_size, &nr_segments, &segment_size);

	C2_SET0(netbuf);
	rc = c2_bufvec_alloc_aligned(&netbuf->nb_buffer, nr_segments,
				     segment_size, C2_SEG_SHIFT);
	if (rc != 0) {
		if (rc == -ENOMEM)
			C2_ADDB_ADD(&c2_rpc_addb_ctx, &c2_rpc_addb_loc,
				    c2_addb_oom);
		C2_LOG(C2_ERROR, "buffer allocation failed");
		goto out;
	}

	rc = c2_net_buffer_register(netbuf, ndom);
	if (rc != 0) {
		C2_LOG(C2_ERROR, "net buf registeration failed");
		c2_bufvec_free_aligned(&netbuf->nb_buffer, C2_SEG_SHIFT);
	}
out:
	C2_RETURN(rc);
}

/**
   Depending on buf_size and maximum network buffer segment size,
   returns number and size of segments to required to carry contents of
   size buf_size.
 */
static void bufvec_geometry(struct c2_net_domain *ndom,
			    c2_bcount_t           buf_size,
			    int32_t              *out_nr_segments,
			    c2_bcount_t          *out_segment_size)
{
	c2_bcount_t max_buf_size;
	c2_bcount_t max_segment_size;
	c2_bcount_t segment_size;
	int32_t     max_nr_segments;
	int32_t     nr_segments;

	C2_ENTRY();

	max_buf_size     = c2_net_domain_get_max_buffer_size(ndom);
	max_segment_size = c2_net_domain_get_max_buffer_segment_size(ndom);
	max_nr_segments  = c2_net_domain_get_max_buffer_segments(ndom);

	C2_LOG(C2_DEBUG,
		"max_buf_size: %llu max_segment_size: %llu max_nr_seg: %d",
		(unsigned long long)max_buf_size,
		(unsigned long long)max_segment_size, max_nr_segments);

	C2_ASSERT(buf_size <= max_buf_size);

	/* encoding routine requires buf_size to be 8 byte aligned */
	buf_size = c2_align(buf_size, 8);
	C2_LOG(C2_DEBUG, "bufsize: 0x%llx", (unsigned long long)buf_size);

	if (buf_size <= max_segment_size) {
		segment_size = buf_size;
		nr_segments  = 1;
	} else {
		segment_size = max_segment_size;

		nr_segments = buf_size / max_segment_size;
		if (buf_size % max_segment_size != 0)
			++nr_segments;
	}

	*out_segment_size = segment_size;
	*out_nr_segments  = nr_segments;

	C2_LEAVE("seg_size: %llu nr_segments: %d",
			(unsigned long long)*out_segment_size,
			*out_nr_segments);
}

static void net_buffer_free(struct c2_net_buffer *netbuf,
			    struct c2_net_domain *ndom)
{
	C2_ENTRY("netbuf: %p ndom: %p", netbuf, ndom);
	C2_PRE(netbuf != NULL && ndom != NULL);

	c2_net_buffer_deregister(netbuf, ndom);
	c2_bufvec_free_aligned(&netbuf->nb_buffer, C2_SEG_SHIFT);

	C2_LEAVE();
}

/**
   Submits buffer to network layer for sending.
 */
static int rpc_buffer_submit(struct rpc_buffer *rpcbuf)
{
	struct c2_net_buffer  *netbuf;
	struct c2_rpc_machine *machine;
	int                    rc;

	C2_ENTRY("rpcbuf: %p", rpcbuf);
	C2_PRE(rpc_buffer_bob_check(rpcbuf));

	netbuf = &rpcbuf->rb_netbuf;

	netbuf->nb_qtype     = C2_NET_QT_MSG_SEND;
	netbuf->nb_callbacks = &outgoing_buf_callbacks;

	machine = rpc_buffer__rmachine(rpcbuf);
	rc = c2_net_buffer_add(netbuf, &machine->rm_tm);

	C2_RETURN(rc);
}

static void rpc_buffer_fini(struct rpc_buffer *rpcbuf)
{
	struct c2_net_domain  *ndom;
	struct c2_rpc_machine *machine;

	C2_ENTRY("rpcbuf: %p", rpcbuf);
	C2_PRE(rpc_buffer_bob_check(rpcbuf));

	machine = rpc_buffer__rmachine(rpcbuf);
	ndom    = machine->rm_tm.ntm_dom;
	C2_ASSERT(ndom != NULL);

	net_buffer_free(&rpcbuf->rb_netbuf, ndom);
	rpc_buffer_bob_fini(rpcbuf);

	C2_LEAVE();
}

/**
   Network layer calls this function, whenever there is any event on
   network buffer which was previously submitted for sending by RPC layer.
 */
static void outgoing_buf_event_handler(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer  *netbuf;
	struct rpc_buffer     *rpcbuf;
	struct c2_rpc_machine *machine;
	struct c2_rpc_stats   *stats;
	struct c2_rpc_packet  *p;

	C2_ENTRY("ev: %p", ev);
	C2_PRE(ev != NULL);

	netbuf = ev->nbe_buffer;
	C2_ASSERT(netbuf != NULL &&
		  netbuf->nb_qtype == C2_NET_QT_MSG_SEND &&
		  (netbuf->nb_flags & C2_NET_BUF_QUEUED) == 0);

	rpcbuf = bob_of(netbuf, struct rpc_buffer, rb_netbuf,
			&rpc_buffer_bob_type);

	machine = rpc_buffer__rmachine(rpcbuf);

	c2_rpc_machine_lock(machine);

	stats = &machine->rm_stats;
	p = rpcbuf->rb_packet;
	p->rp_status = ev->nbe_status;

	if (C2_FI_ENABLED("fake_err")) {
		p->rp_status = -EINVAL;
	}
	rpc_buffer_fini(rpcbuf);
	c2_free(rpcbuf);

	if (p->rp_status == 0) {
		stats->rs_nr_sent_packets++;
		stats->rs_nr_sent_bytes += p->rp_size;
	} else
		stats->rs_nr_failed_packets++;

	c2_rpc_packet_traverse_items(p, item_done, p->rp_status);
	c2_rpc_frm_packet_done(p);
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);

	c2_rpc_machine_unlock(machine);
	C2_LEAVE();
}

static void item_done(struct c2_rpc_item *item, unsigned long rc)
{
	C2_ENTRY("item: %p rc: %lu", item, rc);
	C2_PRE(item != NULL && item->ri_ops != NULL);

	item->ri_error = rc;
	if (item->ri_ops->rio_sent != NULL)
		item->ri_ops->rio_sent(item);

	if (rc == 0) {
		c2_rpc_item_change_state(item, C2_RPC_ITEM_SENT);
		/*
		 * request items will automatically move to
		 * WAITING_FOR_REPLY state.
		 */
		if (c2_rpc_item_is_request(item)) {
			if (item->ri_reply != NULL) {
				/* Reply has already been received when we
				   were waiting for buffer callback */
				c2_rpc_slot_process_reply(item);
			} else {
				c2_rpc_item_start_timer(item);
			}
		}
	} else {
		c2_rpc_item_failed(item, (int32_t)rc);
	}
	/*
	 * Request and Reply items take hold on session until
	 * they are SENT/FAILED.
	 * See: c2_rpc__post_locked(), c2_rpc_reply_post()
	 */
	if (c2_rpc_item_is_bound(item))
		c2_rpc_session_release(item->ri_session);

	C2_LEAVE();
}

/**
   @see c2_rpc_frm_ops::fo_item_bind()
 */
static bool item_bind(struct c2_rpc_item *item)
{
	bool result;

	C2_ENTRY("item: %p", item);
	C2_PRE(item != NULL && c2_rpc_item_is_unbound(item));
	C2_PRE(item->ri_session != NULL);

	result = c2_rpc_session_bind_item(item);

	C2_LEAVE("result: %s", c2_bool_to_str(result));
	return result;
}
