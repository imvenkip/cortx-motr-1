#include "lib/memory.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/misc.h"
#include "rpc/formation2.h"
#include "rpc/packet.h"
#include "rpc/rpc2.h"
#include "net/net.h"
#include "rpc/session_internal.h"

#define ULL unsigned long long

static bool packet_ready(struct c2_rpc_packet  *p,
			 struct c2_rpc_machine *machine,
			 struct c2_rpc_chan    *rchan);
static bool bind_item(struct c2_rpc_item *item);

int net_buffer_allocate(struct c2_net_buffer *netbuf,
			struct c2_net_domain *ndom,
			c2_bcount_t           buf_size);

static void net_buffer_free(struct c2_net_buffer *netbuf,
			    struct c2_net_domain *ndom);

void get_bufvec_geometry(struct c2_net_domain *ndom,
			 c2_bcount_t           buf_size,
			 int32_t              *out_nr_segments,
			 c2_bcount_t          *out_segment_size);

struct c2_rpc_frm_ops c2_rpc_frm_default_ops = {
	.fo_packet_ready = packet_ready,
	.fo_bind_item    = bind_item,
};

struct rpc_buffer {
	struct c2_net_buffer   rb_netbuf;
	struct c2_rpc_packet  *rb_packet;
	struct c2_rpc_machine *rb_machine;
	struct c2_rpc_chan    *rb_rchan;
};

int rpc_buffer_init(struct rpc_buffer     *rpcbuf,
		    struct c2_rpc_packet  *p,
		    struct c2_rpc_machine *mahcine,
		    struct c2_rpc_chan    *rchan);

int rpc_buffer_submit(struct rpc_buffer *rpcbuf);

void rpc_buffer_fini(struct rpc_buffer *rpcbuf);

void out_buffer_event_handler(const struct c2_net_buffer_event *ev);

const struct c2_net_buffer_callbacks send_buf_callbacks = {
	.nbc_cb = {
		[C2_NET_QT_MSG_SEND] = out_buffer_event_handler
	}
};

static bool packet_ready(struct c2_rpc_packet  *p,
			 struct c2_rpc_machine *machine,
			 struct c2_rpc_chan    *rchan)
{
	struct rpc_buffer *rpcbuf;
	int                rc;

	C2_ENTRY("packet: %p", p);
	C2_PRE(c2_rpc_packet_invariant(p));

	C2_ALLOC_PTR(rpcbuf);
	if (rpcbuf == NULL) {
		C2_LEAVE("Failed to allocate memory");
		/** @todo XXX store packet somewhere */
		return false;
	}
	rc = rpc_buffer_init(rpcbuf, p, machine, rchan);
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

	c2_rpc_packet_failed(p, rc);
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);

	C2_LEAVE("false");
	return false;
}

int rpc_buffer_init(struct rpc_buffer     *rpcbuf,
		    struct c2_rpc_packet  *p,
		    struct c2_rpc_machine *machine,
		    struct c2_rpc_chan    *rchan)
{
	struct c2_net_buffer *netbuf;
	struct c2_net_domain *ndom;
	int                   rc;

	C2_ENTRY("rbuf: %p packet: %p machine: %p rchan: %p",
		 rpcbuf, p, machine, rchan);
	C2_PRE(rpcbuf != NULL &&
	       p != NULL &&
	       machine != NULL &&
	       rchan != NULL);

	ndom = machine->rm_tm.ntm_dom;
	C2_ASSERT(ndom != NULL);

	netbuf = &rpcbuf->rb_netbuf;
	rc = net_buffer_allocate(netbuf, ndom, p->rp_size);
	if (rc != 0)
		goto out;

	rc = c2_rpc_packet_encode_in_buf(p, &netbuf->nb_buffer);
	if (rc != 0) {
		net_buffer_free(netbuf, ndom);
		goto out;
	}
	netbuf->nb_length  = p->rp_size;
	netbuf->nb_ep      = rchan->rc_destep;

	rpcbuf->rb_packet  = p;
	rpcbuf->rb_machine = machine;
	rpcbuf->rb_rchan   = rchan;

out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

int net_buffer_allocate(struct c2_net_buffer *netbuf,
			struct c2_net_domain *ndom,
			c2_bcount_t           buf_size)
{
	c2_bcount_t segment_size;
	int32_t     nr_segments;
	int         rc;

	C2_ENTRY("netbuf: %p ndom: %p bufsize: %llu", netbuf, ndom,
						      (ULL)buf_size);
	C2_PRE(netbuf != NULL && ndom != NULL && buf_size > 0);

	get_bufvec_geometry(ndom, buf_size, &nr_segments, &segment_size);

	C2_SET0(netbuf);
	rc = c2_bufvec_alloc(&netbuf->nb_buffer, nr_segments, segment_size);
	if (rc != 0) {
		C2_LOG("buffer allocation failed");
		goto out;
	}

	rc = c2_net_buffer_register(netbuf, ndom);
	if (rc != 0) {
		C2_LOG("net buf registeration failed");
		c2_bufvec_free(&netbuf->nb_buffer);
	}
out:
	C2_LEAVE("rc: %d", rc);
	return rc;
}

void get_bufvec_geometry(struct c2_net_domain *ndom,
			 c2_bcount_t           buf_size,
			 int32_t              *out_nr_segments,
			 c2_bcount_t          *out_segment_size)
{
	c2_bcount_t max_buf_size;
	c2_bcount_t max_segment_size;
	c2_bcount_t segment_size;
	int32_t     max_nr_segments;
	int32_t     nr_segments;
	uint32_t    align;

	C2_ENTRY();

	max_buf_size     = c2_net_domain_get_max_buffer_size(ndom);
	max_segment_size = c2_net_domain_get_max_buffer_segment_size(ndom);
	max_nr_segments  = c2_net_domain_get_max_buffer_segments(ndom);

	C2_LOG("max_buf_size: %llu max_segment_size: %llu max_nr_seg: %d",
	       (ULL)max_buf_size, (ULL)max_segment_size, max_nr_segments);

	C2_ASSERT(buf_size <= max_buf_size);

	/* encoding routine requires buf_size to be 8 byte aligned */
	align = 8;
	buf_size = (buf_size + align - 1) & ~(align - 1);
	C2_LOG("bufsize: 0x%llx", (ULL)buf_size);
	C2_ASSERT(C2_IS_8ALIGNED(buf_size));

	if (buf_size < max_segment_size) {
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

	C2_LEAVE("seg_size: %llu nr_segments: %d", (ULL)segment_size,
						   nr_segments);
}

static void net_buffer_free(struct c2_net_buffer *netbuf,
			    struct c2_net_domain *ndom)
{
	C2_ENTRY("netbuf: %p ndom: %p", netbuf, ndom);
	C2_PRE(netbuf != NULL && ndom != NULL);

	c2_net_buffer_deregister(netbuf, ndom);
	c2_bufvec_free(&netbuf->nb_buffer);

	C2_LEAVE();
}

int rpc_buffer_submit(struct rpc_buffer *rpcbuf)
{
	struct c2_net_buffer *netbuf;
	int                   rc;

	C2_ENTRY("rpcbuf: %p", rpcbuf);
	C2_PRE(rpcbuf != NULL &&
	       rpcbuf->rb_machine != NULL);

	netbuf = &rpcbuf->rb_netbuf;

	netbuf->nb_qtype     = C2_NET_QT_MSG_SEND;
	netbuf->nb_callbacks = &send_buf_callbacks;

	rc = c2_net_buffer_add(netbuf, &rpcbuf->rb_machine->rm_tm);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

void rpc_buffer_fini(struct rpc_buffer *rpcbuf)
{
	struct c2_net_domain *ndom;

	C2_ENTRY("rpcbuf: %p", rpcbuf);
	C2_PRE(rpcbuf != NULL && rpcbuf->rb_machine != NULL);
	ndom = rpcbuf->rb_machine->rm_tm.ntm_dom;
	C2_ASSERT(ndom != NULL);

	net_buffer_free(&rpcbuf->rb_netbuf, ndom);
	C2_SET0(rpcbuf);

	C2_LEAVE();
}

void out_buffer_event_handler(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer  *netbuf;
	struct rpc_buffer     *rpcbuf;
	struct c2_rpc_machine *machine;
	struct c2_rpc_packet  *p;

	C2_ENTRY("ev: %p", ev);
	C2_PRE(ev != NULL);

	netbuf = ev->nbe_buffer;
	C2_ASSERT(netbuf != NULL &&
		  netbuf->nb_qtype == C2_NET_QT_MSG_SEND &&
		  (netbuf->nb_flags & C2_NET_BUF_QUEUED) == 0);

	rpcbuf = container_of(netbuf, struct rpc_buffer, rb_netbuf);
	C2_ASSERT(rpcbuf->rb_machine != NULL);

	machine = rpcbuf->rb_machine;
	c2_rpc_machine_lock(machine);

	p = rpcbuf->rb_packet;
	if (ev->nbe_status == 0)
		c2_rpc_packet_sent(p);
	else
		c2_rpc_packet_failed(p, ev->nbe_status);

	c2_chan_broadcast(&p->rp_chan);
	c2_rpc_packet_remove_all_items(p);
	c2_rpc_packet_fini(p);
	c2_free(p);
	rpc_buffer_fini(rpcbuf);
	c2_free(rpcbuf);

	c2_rpc_machine_unlock(machine);	
	C2_LEAVE();
}

static bool bind_item(struct c2_rpc_item *item)
{
	bool result;

	C2_ENTRY("item: %p", item);
	C2_PRE(item != NULL && c2_rpc_item_is_unbound(item));
	C2_PRE(item->ri_session != NULL);

	result = c2_rpc_session_bind_item(item);

	C2_LEAVE("result: %s", result ? "true" : "false");
	return result;
}
