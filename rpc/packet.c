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
 * Original creation date: 05/25/2012
 */

#include "lib/tlist.h"
#include "lib/misc.h"
#include "lib/vec.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "rpc/packet.h"
#include "rpc/rpc2.h"
#include "rpc/rpc_onwire.h"

static int packet_header_encode(struct c2_rpc_packet    *p,
				struct c2_bufvec_cursor *cursor);
static int item_encode(struct c2_rpc_item       *item,
		       struct c2_bufvec_cursor  *cursor);

enum {
	PACKET_HEAD_MAGIC = 0x525041434b4554 /* "RPACKET" */
};
C2_TL_DESCR_DEFINE(packet_item, "packet_item", static, struct c2_rpc_item,
                   ri_plink, ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
                   PACKET_HEAD_MAGIC);
C2_TL_DEFINE(packet_item, static, struct c2_rpc_item);

bool c2_rpc_packet_invariant(const struct c2_rpc_packet *p)
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

	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_LEAVE();
}

void c2_rpc_packet_fini(struct c2_rpc_packet *p)
{
	C2_ENTRY("packet: %p nr_items: %llu", p,
					   (unsigned long long)p->rp_nr_items);
	C2_PRE(c2_rpc_packet_invariant(p) && p->rp_nr_items == 0);

	packet_item_tlist_fini(&p->rp_items);
	C2_SET0(p);

	C2_LEAVE();
}

void c2_rpc_packet_add_item(struct c2_rpc_packet *p,
			    struct c2_rpc_item   *item)
{
	C2_ENTRY("packet: %p item: %p", p, item);
	C2_PRE(c2_rpc_packet_invariant(p) && item != NULL);
	C2_PRE(!c2_rpc_packet_is_carrying_item(p, item));

	packet_item_tlink_init_at_tail(item, &p->rp_items);
	++p->rp_nr_items;
	p->rp_size += c2_rpc_item_size(item);

	C2_LOG("nr_items: %llu packet size: %llu",
			(unsigned long long)p->rp_nr_items,
			(unsigned long long)p->rp_size);
	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_POST(c2_rpc_packet_is_carrying_item(p, item));
	C2_LEAVE();
}

void c2_rpc_packet_remove_item(struct c2_rpc_packet *p,
			       struct c2_rpc_item   *item)
{
	C2_ENTRY("packet: %p item: %p", p, item);
	C2_PRE(c2_rpc_packet_invariant(p) && item != NULL);
	C2_PRE(c2_rpc_packet_is_carrying_item(p, item));

	packet_item_tlink_del_fini(item);
	--p->rp_nr_items;
	p->rp_size -= c2_rpc_item_size(item);

	C2_LOG("nr_items: %llu packet size: %llu",
			(unsigned long long)p->rp_nr_items,
			(unsigned long long)p->rp_size);
	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_POST(!c2_rpc_packet_is_carrying_item(p, item));
	C2_LEAVE();
}

void c2_rpc_packet_remove_all_items(struct c2_rpc_packet *p)
{
	struct c2_rpc_item *item;

	C2_ENTRY("packet: %p", p);
	C2_PRE(c2_rpc_packet_invariant(p) && !c2_rpc_packet_is_empty(p));
	C2_LOG("nr_items: %d", (int)p->rp_nr_items);

	c2_tl_for(packet_item, &p->rp_items, item)
		c2_rpc_packet_remove_item(p, item);
	c2_tl_endfor;

	C2_POST(c2_rpc_packet_invariant(p) && c2_rpc_packet_is_empty(p));
	C2_LEAVE();
}

bool c2_rpc_packet_is_carrying_item(const struct c2_rpc_packet *p,
				    const struct c2_rpc_item   *item)
{
	return packet_item_tlist_contains(&p->rp_items, item);
}

bool c2_rpc_packet_is_empty(const struct c2_rpc_packet *p)
{
	C2_PRE(c2_rpc_packet_invariant(p));

	return p->rp_nr_items == 0;
}

int c2_rpc_packet_encode_in_buf(struct c2_rpc_packet *p,
				struct c2_bufvec     *bufvec)
{
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              bufvec_size;
	int                      rc;

	C2_ENTRY("packet: %p bufvec: %p", p, bufvec);
	C2_PRE(c2_rpc_packet_invariant(p) && bufvec != NULL);
	C2_PRE(!c2_rpc_packet_is_empty(p));

	bufvec_size = c2_vec_count(&bufvec->ov_vec);

	C2_ASSERT(C2_IS_8ALIGNED(bufvec_size));
	C2_ASSERT(c2_forall(i, bufvec->ov_vec.v_nr,
			    C2_IS_8ALIGNED(bufvec->ov_vec.v_count[i])));
	C2_ASSERT(bufvec_size >= p->rp_size);

	c2_bufvec_cursor_init(&cur, bufvec);
	C2_ASSERT(C2_IS_8ALIGNED(c2_bufvec_cursor_addr(&cur)));

	rc = c2_rpc_packet_encode_using_cursor(p, &cur);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

int c2_rpc_packet_encode_using_cursor(struct c2_rpc_packet    *packet,
				      struct c2_bufvec_cursor *cursor)
{
	struct c2_rpc_item *item;
	int                 rc;

	C2_ENTRY("packet: %p cursor: %p", packet, cursor);
	C2_PRE(c2_rpc_packet_invariant(packet) && cursor != NULL);
	C2_PRE(!c2_rpc_packet_is_empty(packet));

	rc = packet_header_encode(packet, cursor);
	if (rc == 0) {
		c2_tl_for(packet_item, &packet->rp_items, item) {
			rc = item_encode(item, cursor);
			if (rc != 0)
				break;
		} c2_tl_endfor;
	}

	C2_LEAVE("rc: %d", rc);
	return rc;
}

/*
 * packet_header_encode() and item_encode() have similar/same counterparts in
 * rpc_onwire.c. This plagiarisation is intentional and needed, until we
 * change all the places in RPC layer using c2_rpc.
 */
static int packet_header_encode(struct c2_rpc_packet    *p,
				struct c2_bufvec_cursor *cursor)
{
	uint32_t ver;
	int      rc;

	C2_ENTRY();
	ver = C2_RPC_VERSION_1;
	rc = c2_bufvec_uint32(cursor, &ver, C2_BUFVEC_ENCODE) ?:
	     c2_bufvec_uint32(cursor, &p->rp_nr_items, C2_BUFVEC_ENCODE);

	C2_LEAVE("rc: %d", rc);
	return rc;
}

static int item_encode(struct c2_rpc_item       *item,
		       struct c2_bufvec_cursor  *cursor)
{
	int rc;

	C2_ENTRY("item: %p cursor: %p", item, cursor);
	C2_PRE(item != NULL && cursor != NULL);
	C2_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_encode != NULL);

	rc = item->ri_type->rit_ops->rito_encode(item->ri_type, item, cursor);
	C2_LEAVE("rc: %d", rc);
	return rc;
}

void c2_rpc_packet_traverse_items(struct c2_rpc_packet *p,
				  item_visit_fn        *visit,
				  unsigned long         opaque_data)
{
	struct c2_rpc_item *item;

	C2_ENTRY("p: %p visit: %p", p, visit);
	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_LOG("nr_items: %u", (unsigned int)p->rp_nr_items);

	c2_tl_for(packet_item, &p->rp_items, item) {
		visit(item, opaque_data);
	} c2_tl_endfor;

	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_LEAVE();
}
