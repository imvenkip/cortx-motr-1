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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/misc.h"
#include "lib/vec.h"
#include "lib/errno.h"
#include "lib/finject.h"
#include "mero/magic.h"
#include "xcode/xcode.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

/**
 * @addtogroup rpc
 * @{
 */

#define PACKHD_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_rpc_packet_onwire_header_xc, ptr)

static int packet_header_encode(struct m0_rpc_packet_onwire_header *ph,
				struct m0_bufvec_cursor            *cursor);

static int packet_header_decode(struct m0_bufvec_cursor            *cursor,
				struct m0_rpc_packet_onwire_header *ph);

static int item_encode(struct m0_rpc_item       *item,
		       struct m0_bufvec_cursor  *cursor);
static int item_decode(struct m0_bufvec_cursor  *cursor,
		       struct m0_rpc_item      **item_out);

M0_TL_DESCR_DEFINE(packet_item, "packet_item", M0_INTERNAL, struct m0_rpc_item,
                   ri_plink, ri_magic, M0_RPC_ITEM_MAGIC,
                   M0_RPC_PACKET_HEAD_MAGIC);
M0_TL_DEFINE(packet_item, M0_INTERNAL, struct m0_rpc_item);

M0_INTERNAL m0_bcount_t m0_rpc_packet_onwire_header_size(void)
{
	struct m0_rpc_packet_onwire_header oh;
	struct m0_xcode_ctx                ctx;
	static m0_bcount_t                 packet_header_size;

	if (packet_header_size == 0) {
		m0_xcode_ctx_init(&ctx, &PACKHD_XCODE_OBJ(&oh));
		packet_header_size = m0_xcode_length(&ctx);
	}

	return packet_header_size;
}

M0_INTERNAL bool m0_rpc_packet_invariant(const struct m0_rpc_packet *p)
{
	struct m0_rpc_item *item;
	m0_bcount_t         size;

	size = 0;
	for_each_item_in_packet(item, p)
		size += m0_rpc_item_size(item)
	end_for_each_item_in_packet;

	return
		p != NULL &&
		p->rp_ow.poh_version != 0 &&
		p->rp_ow.poh_magic == M0_RPC_PACKET_HEAD_MAGIC &&
		p->rp_ow.poh_nr_items ==
			packet_item_tlist_length(&p->rp_items) &&
		p->rp_size == size + m0_rpc_packet_onwire_header_size();
}

M0_INTERNAL void m0_rpc_packet_init(struct m0_rpc_packet *p)
{
	M0_ENTRY("packet: %p", p);
	M0_PRE(p != NULL);

	M0_SET0(p);

	p->rp_ow.poh_version = M0_RPC_VERSION_1;
	p->rp_ow.poh_magic = M0_RPC_PACKET_HEAD_MAGIC;
	p->rp_size = m0_rpc_packet_onwire_header_size();
	packet_item_tlist_init(&p->rp_items);

	M0_ASSERT(m0_rpc_packet_invariant(p));
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_packet_fini(struct m0_rpc_packet *p)
{
	M0_ENTRY("packet: %p nr_items: %llu", p,
		 (unsigned long long)p->rp_ow.poh_nr_items);
	M0_PRE(m0_rpc_packet_invariant(p) && p->rp_ow.poh_nr_items == 0);

	packet_item_tlist_fini(&p->rp_items);
	M0_SET0(p);

	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_packet_add_item(struct m0_rpc_packet *p,
					struct m0_rpc_item *item)
{
	M0_ENTRY("packet: %p item: %p", p, item);
	M0_PRE(m0_rpc_packet_invariant(p) && item != NULL);
	M0_PRE(!packet_item_tlink_is_in(item));

	packet_item_tlink_init_at_tail(item, &p->rp_items);
	++p->rp_ow.poh_nr_items;
	p->rp_size += m0_rpc_item_size(item);

	M0_LOG(M0_DEBUG, "nr_items: %llu packet size: %llu",
			(unsigned long long)p->rp_ow.poh_nr_items,
			(unsigned long long)p->rp_size);
	M0_ASSERT(m0_rpc_packet_invariant(p));
	M0_POST(m0_rpc_packet_is_carrying_item(p, item));
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_packet_remove_item(struct m0_rpc_packet *p,
					   struct m0_rpc_item *item)
{
	M0_ENTRY("packet: %p item: %p", p, item);
	M0_PRE(m0_rpc_packet_invariant(p) && item != NULL);
	M0_PRE(m0_rpc_packet_is_carrying_item(p, item));

	packet_item_tlink_del_fini(item);
	--p->rp_ow.poh_nr_items;
	p->rp_size -= m0_rpc_item_size(item);

	M0_LOG(M0_DEBUG, "nr_items: %llu packet size: %llu",
			(unsigned long long)p->rp_ow.poh_nr_items,
			(unsigned long long)p->rp_size);
	M0_ASSERT(m0_rpc_packet_invariant(p));
	M0_POST(!packet_item_tlink_is_in(item));
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_packet_remove_all_items(struct m0_rpc_packet *p)
{
	struct m0_rpc_item *item;

	M0_ENTRY("packet: %p", p);
	M0_PRE(m0_rpc_packet_invariant(p));
	M0_LOG(M0_DEBUG, "nr_items: %d", (int)p->rp_ow.poh_nr_items);

	for_each_item_in_packet(item, p)
		m0_rpc_packet_remove_item(p, item);
	end_for_each_item_in_packet;

	M0_POST(m0_rpc_packet_invariant(p) && m0_rpc_packet_is_empty(p));
	M0_LEAVE();
}

M0_INTERNAL bool m0_rpc_packet_is_carrying_item(const struct m0_rpc_packet *p,
						const struct m0_rpc_item *item)
{
	return packet_item_tlist_contains(&p->rp_items, item);
}

M0_INTERNAL bool m0_rpc_packet_is_empty(const struct m0_rpc_packet *p)
{
	M0_PRE(m0_rpc_packet_invariant(p));

	return p->rp_ow.poh_nr_items == 0;
}

M0_INTERNAL int m0_rpc_packet_encode(struct m0_rpc_packet *p,
				     struct m0_bufvec *bufvec)
{
	struct m0_bufvec_cursor cur;
	m0_bcount_t             bufvec_size;

	M0_ENTRY("packet: %p bufvec: %p", p, bufvec);
	M0_PRE(m0_rpc_packet_invariant(p) && bufvec != NULL);
	M0_PRE(!m0_rpc_packet_is_empty(p));

	if (M0_FI_ENABLED("fake_error"))
		return -EFAULT;

	bufvec_size = m0_vec_count(&bufvec->ov_vec);

	M0_ASSERT(M0_IS_8ALIGNED(bufvec_size));
	M0_ASSERT(m0_forall(i, bufvec->ov_vec.v_nr,
			    M0_IS_8ALIGNED(bufvec->ov_vec.v_count[i])));
	M0_ASSERT(bufvec_size >= p->rp_size);

	m0_bufvec_cursor_init(&cur, bufvec);
	M0_ASSERT(M0_IS_8ALIGNED(m0_bufvec_cursor_addr(&cur)));

	M0_RETURN(m0_rpc_packet_encode_using_cursor(p, &cur));
}

M0_INTERNAL int m0_rpc_packet_encode_using_cursor(struct m0_rpc_packet *packet,
						  struct m0_bufvec_cursor
						  *cursor)
{
	struct m0_rpc_item *item;
	bool                end_of_bufvec;
	int                 rc;

	M0_ENTRY("packet: %p cursor: %p", packet, cursor);
	M0_PRE(m0_rpc_packet_invariant(packet) && cursor != NULL);
	M0_PRE(!m0_rpc_packet_is_empty(packet));

	rc = packet_header_encode(&packet->rp_ow, cursor);
	if (rc == 0) {
		for_each_item_in_packet(item, packet) {
			rc = item_encode(item, cursor);
			if (rc != 0)
				break;
		} end_for_each_item_in_packet;
	}
	end_of_bufvec = m0_bufvec_cursor_align(cursor, 8);
	M0_ASSERT(end_of_bufvec ||
		  M0_IS_8ALIGNED(m0_bufvec_cursor_addr(cursor)));
	M0_RETURN(rc);
}

static int packet_header_encode(struct m0_rpc_packet_onwire_header *ph,
				struct m0_bufvec_cursor            *cursor)

{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY();

	m0_xcode_ctx_init(&ctx, &PACKHD_XCODE_OBJ(ph));
	ctx.xcx_buf = *cursor;
	rc = m0_xcode_encode(&ctx);
	if (rc == 0)
		*cursor = ctx.xcx_buf;

	M0_RETURN(rc);
}

static int packet_header_decode(struct m0_bufvec_cursor            *cursor,
				struct m0_rpc_packet_onwire_header *ph)
{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY();
	M0_PRE(cursor != NULL && ph != NULL);

	m0_xcode_ctx_init(&ctx, &PACKHD_XCODE_OBJ(NULL));
	ctx.xcx_buf   = *cursor;
	ctx.xcx_alloc = m0_xcode_alloc;

	rc = m0_xcode_decode(&ctx);
	if (rc == 0) {
		*ph     = *(struct m0_rpc_packet_onwire_header *)
				m0_xcode_ctx_top(&ctx);
		*cursor = ctx.xcx_buf;
		m0_xcode_free(&PACKHD_XCODE_OBJ(m0_xcode_ctx_top(&ctx)));
	}

	M0_RETURN(rc);
}

static int item_encode(struct m0_rpc_item       *item,
		       struct m0_bufvec_cursor  *cursor)
{
	struct m0_rpc_item_onwire_header ioh;
	int                              rc;

	M0_ENTRY("item: %p cursor: %p", item, cursor);
	M0_PRE(item != NULL && cursor != NULL);
	M0_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_encode != NULL);

	ioh = (struct m0_rpc_item_onwire_header){
		.ioh_opcode = item->ri_type->rit_opcode,
		.ioh_magic  = M0_RPC_ITEM_MAGIC,
	};

	rc = m0_rpc_item_header_encode(&ioh, cursor);
	if (rc == 0)
		rc = item->ri_type->rit_ops->rito_encode(item->ri_type,
							 item, cursor);
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_packet_decode(struct m0_rpc_packet *p,
				     struct m0_bufvec *bufvec,
				     m0_bindex_t off, m0_bcount_t len)
{
	struct m0_bufvec_cursor cursor;
	int                     rc;

	M0_ENTRY();
	M0_PRE(m0_rpc_packet_invariant(p) && bufvec != NULL && len > 0);
	M0_PRE(len <= m0_vec_count(&bufvec->ov_vec));
	M0_PRE(M0_IS_8ALIGNED(off) && M0_IS_8ALIGNED(len));
	M0_ASSERT(m0_forall(i, bufvec->ov_vec.v_nr,
			    M0_IS_8ALIGNED(bufvec->ov_vec.v_count[i])));

	m0_bufvec_cursor_init(&cursor, bufvec);
	m0_bufvec_cursor_move(&cursor, off);
	M0_ASSERT(M0_IS_8ALIGNED(m0_bufvec_cursor_addr(&cursor)));
	rc = m0_rpc_packet_decode_using_cursor(p, &cursor, len);
	M0_ASSERT(m0_bufvec_cursor_move(&cursor, 0) ||
		  M0_IS_8ALIGNED(m0_bufvec_cursor_addr(&cursor)));
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_packet_decode_using_cursor(struct m0_rpc_packet *p,
						  struct m0_bufvec_cursor
						  *cursor, m0_bcount_t len)
{
	struct m0_rpc_packet_onwire_header poh;
	struct m0_rpc_item                *item;
	int                                rc;
	int                                i;

	M0_ENTRY();
	M0_PRE(m0_rpc_packet_invariant(p) && cursor != NULL);

	rc = packet_header_decode(cursor, &poh);
	if (rc != 0)
		M0_RETURN(rc);
	if (poh.poh_version != M0_RPC_VERSION_1 || poh.poh_nr_items == 0 ||
	    poh.poh_magic != M0_RPC_PACKET_HEAD_MAGIC)
		M0_RETURN(-EPROTO);

	for (i = 0; i < poh.poh_nr_items; ++i) {
		rc = item_decode(cursor, &item);
		if (rc != 0)
			M0_RETURN(rc);
		m0_rpc_packet_add_item(p, item);
		item = NULL;
	}
	m0_bufvec_cursor_align(cursor, 8);
	M0_ASSERT(m0_rpc_packet_invariant(p));

	M0_RETURN(0);
}

static int item_decode(struct m0_bufvec_cursor  *cursor,
		       struct m0_rpc_item      **item_out)
{
	struct m0_rpc_item_type         *item_type;
	struct m0_rpc_item_onwire_header ioh;
	int                              rc;

	M0_ENTRY();
	M0_PRE(cursor != NULL && item_out != NULL);

	rc = m0_rpc_item_header_decode(cursor, &ioh);
	if (rc != 0)
		M0_RETURN(rc);

	if (ioh.ioh_magic != M0_RPC_ITEM_MAGIC)
		return -EPROTO;

	*item_out = NULL;

	item_type = m0_rpc_item_type_lookup(ioh.ioh_opcode);
	if (item_type == NULL)
		M0_RETURN(-EPROTO);

	M0_ASSERT(item_type->rit_ops != NULL &&
		  item_type->rit_ops->rito_decode != NULL);

	return item_type->rit_ops->rito_decode(item_type, item_out, cursor);
}

M0_INTERNAL void m0_rpc_packet_traverse_items(struct m0_rpc_packet *p,
					      item_visit_fn *visit,
					      unsigned long opaque_data)
{
	struct m0_rpc_item *item;

	M0_ENTRY("p: %p visit: %p", p, visit);
	M0_ASSERT(m0_rpc_packet_invariant(p));
	M0_LOG(M0_DEBUG, "nr_items: %u", (unsigned int)p->rp_ow.poh_nr_items);

	for_each_item_in_packet(item, p) {
		visit(item, opaque_data);
	} end_for_each_item_in_packet;

	M0_ASSERT(m0_rpc_packet_invariant(p));
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
