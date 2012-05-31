#include "lib/tlist.h"
#include "lib/chan.h"
#include "lib/misc.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "rpc/packet.h"
#include "rpc/rpc2.h"

#define ULL unsigned long long

enum {
	PACKET_HEAD_MAGIC = 0x1111000011110000
};
C2_TL_DESCR_DEFINE(packet_item, "packet_item", static, struct c2_rpc_item,
                   ri_plink, ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
                   PACKET_HEAD_MAGIC);
C2_TL_DEFINE(packet_item, static, struct c2_rpc_item);

const bool packet_invariant(const struct c2_rpc_packet *p)
{
	c2_bcount_t size;

	size = 0;
	return p != NULL &&
	       p->rp_nr_items == packet_item_tlist_length(&p->rp_items) &&
	       c2_tl_forall(packet_item, item, &p->rp_items,
				size += c2_rpc_item_size(item);
				(c2_rpc_item_is_unsolicited(item) ||
				      c2_rpc_item_is_bound(item))) &&
	       p->rp_size == size + C2_RPC_PACKET_OW_HEADER_SIZE;
}

void c2_rpc_packet_init(struct c2_rpc_packet *p)
{
	C2_ENTRY("packet: %p", p);
	C2_PRE(p != NULL);

	C2_SET0(p);
	p->rp_size = C2_RPC_PACKET_OW_HEADER_SIZE;
	packet_item_tlist_init(&p->rp_items);
	c2_chan_init(&p->rp_chan);

	C2_ASSERT(packet_invariant(p));
	C2_LEAVE();
}

void c2_rpc_packet_fini(struct c2_rpc_packet *p)
{
	C2_ENTRY("packet: %p nr_items: %llu", p, (ULL)p->rp_nr_items);
	C2_PRE(packet_invariant(p) && p->rp_nr_items == 0);

	c2_chan_fini(&p->rp_chan);
	packet_item_tlist_fini(&p->rp_items);
	C2_SET0(p);

	C2_LEAVE();
}

void c2_rpc_packet_add_item(struct c2_rpc_packet *p,
			    struct c2_rpc_item   *item)
{
	C2_ENTRY("packet: %p item: %p", p, item);
	C2_PRE(packet_invariant(p) && item != NULL);

	packet_item_tlink_init_at_tail(item, &p->rp_items);
	++p->rp_nr_items;
	p->rp_size += c2_rpc_item_size(item);

	C2_LOG("nr_items: %llu packet size: %llu",
			(ULL)p->rp_nr_items,
			(ULL)p->rp_size);
	C2_ASSERT(packet_invariant(p));
	C2_POST(c2_rpc_packet_is_carrying_item(p, item));
	C2_LEAVE();
}

void c2_rpc_packet_remove_item(struct c2_rpc_packet *p,
			       struct c2_rpc_item   *item)
{
	C2_ENTRY("packet: %p item: %p", p, item);
	C2_PRE(packet_invariant(p) && item != NULL);
	C2_PRE(c2_rpc_packet_is_carrying_item(p, item));

	packet_item_tlink_del_fini(item);
	--p->rp_nr_items;
	p->rp_size -= c2_rpc_item_size(item);

	C2_LOG("nr_items: %llu packet size: %llu", (ULL)p->rp_nr_items,
						   (ULL)p->rp_size);
	C2_ASSERT(packet_invariant(p));
	C2_POST(!c2_rpc_packet_is_carrying_item(p, item));
	C2_LEAVE();
}

void c2_rpc_packet_remove_all_items(struct c2_rpc_packet *p)
{
	struct c2_rpc_item *item;

	C2_ENTRY("packet: %p", p);
	C2_PRE(packet_invariant(p) && !c2_rpc_packet_is_empty(p));
	C2_LOG("nr_items: %d", (int)p->rp_nr_items);

	c2_tl_for(packet_item, &p->rp_items, item)
		c2_rpc_packet_remove_item(p, item);
	c2_tl_endfor;

	C2_POST(packet_invariant(p) && c2_rpc_packet_is_empty(p));
	C2_LEAVE();
}

bool c2_rpc_packet_is_carrying_item(const struct c2_rpc_packet *p,
				    const struct c2_rpc_item   *item)
{
	return packet_item_tlist_contains(&p->rp_items, item);
}

bool c2_rpc_packet_is_empty(const struct c2_rpc_packet *p)
{
	C2_PRE(packet_invariant(p));

	return p->rp_nr_items == 0;
}
