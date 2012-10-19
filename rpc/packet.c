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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/misc.h"
#include "lib/vec.h"
#include "lib/errno.h"
#include "lib/finject.h"
#include "colibri/magic.h"
#include "xcode/xcode.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

/**
 * @addtogroup rpc
 * @{
 */

#define PACKHD_XCODE_OBJ(ptr) C2_XCODE_OBJ(c2_rpc_packet_onwire_header_xc, ptr)

static int packet_header_encode(struct c2_rpc_packet_onwire_header *ph,
				struct c2_bufvec_cursor            *cursor);

static int packet_header_decode(struct c2_bufvec_cursor            *cursor,
				struct c2_rpc_packet_onwire_header *ph);

static int item_encode(struct c2_rpc_item       *item,
		       struct c2_bufvec_cursor  *cursor);
static int item_decode(struct c2_bufvec_cursor  *cursor,
		       struct c2_rpc_item      **item_out);

C2_TL_DESCR_DEFINE(packet_item, "packet_item", /* global */, struct c2_rpc_item,
                   ri_plink, ri_magic, C2_RPC_ITEM_MAGIC,
                   C2_RPC_PACKET_HEAD_MAGIC);
C2_TL_DEFINE(packet_item, /* global */, struct c2_rpc_item);

c2_bcount_t c2_rpc_packet_onwire_header_size(void)
{
	struct c2_rpc_packet_onwire_header oh;
	struct c2_xcode_ctx                ctx;
	static c2_bcount_t                 packet_header_size;

	if (packet_header_size == 0) {
		c2_xcode_ctx_init(&ctx, &PACKHD_XCODE_OBJ(&oh));
		packet_header_size = c2_xcode_length(&ctx);
	}

	return packet_header_size;
}

bool c2_rpc_packet_invariant(const struct c2_rpc_packet *p)
{
	struct c2_rpc_item *item;
	c2_bcount_t         size;

	size = 0;
	for_each_item_in_packet(item, p)
		size += c2_rpc_item_size(item)
	end_for_each_item_in_packet;

	return
		p != NULL &&
		p->rp_ow.poh_version != 0 &&
		p->rp_ow.poh_magic == C2_RPC_PACKET_HEAD_MAGIC &&
		p->rp_ow.poh_nr_items ==
			packet_item_tlist_length(&p->rp_items) &&
		p->rp_size == size + c2_rpc_packet_onwire_header_size();
}

void c2_rpc_packet_init(struct c2_rpc_packet *p)
{
	C2_ENTRY("packet: %p", p);
	C2_PRE(p != NULL);

	C2_SET0(p);

	p->rp_ow.poh_version = C2_RPC_VERSION_1;
	p->rp_ow.poh_magic = C2_RPC_PACKET_HEAD_MAGIC;
	p->rp_size = c2_rpc_packet_onwire_header_size();
	packet_item_tlist_init(&p->rp_items);

	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_LEAVE();
}

void c2_rpc_packet_fini(struct c2_rpc_packet *p)
{
	C2_ENTRY("packet: %p nr_items: %llu", p,
		 (unsigned long long)p->rp_ow.poh_nr_items);
	C2_PRE(c2_rpc_packet_invariant(p) && p->rp_ow.poh_nr_items == 0);

	packet_item_tlist_fini(&p->rp_items);
	C2_SET0(p);

	C2_LEAVE();
}

void c2_rpc_packet_add_item(struct c2_rpc_packet *p,
			    struct c2_rpc_item   *item)
{
	C2_ENTRY("packet: %p item: %p", p, item);
	C2_PRE(c2_rpc_packet_invariant(p) && item != NULL);
	C2_PRE(!packet_item_tlink_is_in(item));

	packet_item_tlink_init_at_tail(item, &p->rp_items);
	++p->rp_ow.poh_nr_items;
	p->rp_size += c2_rpc_item_size(item);

	C2_LOG(C2_DEBUG, "nr_items: %llu packet size: %llu",
			(unsigned long long)p->rp_ow.poh_nr_items,
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
	--p->rp_ow.poh_nr_items;
	p->rp_size -= c2_rpc_item_size(item);

	C2_LOG(C2_DEBUG, "nr_items: %llu packet size: %llu",
			(unsigned long long)p->rp_ow.poh_nr_items,
			(unsigned long long)p->rp_size);
	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_POST(!packet_item_tlink_is_in(item));
	C2_LEAVE();
}

void c2_rpc_packet_remove_all_items(struct c2_rpc_packet *p)
{
	struct c2_rpc_item *item;

	C2_ENTRY("packet: %p", p);
	C2_PRE(c2_rpc_packet_invariant(p));
	C2_LOG(C2_DEBUG, "nr_items: %d", (int)p->rp_ow.poh_nr_items);

	for_each_item_in_packet(item, p)
		c2_rpc_packet_remove_item(p, item);
	end_for_each_item_in_packet;

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

	return p->rp_ow.poh_nr_items == 0;
}

int c2_rpc_packet_encode(struct c2_rpc_packet *p,
			 struct c2_bufvec     *bufvec)
{
	struct c2_bufvec_cursor  cur;
	c2_bcount_t              bufvec_size;
	int                      rc;

	C2_ENTRY("packet: %p bufvec: %p", p, bufvec);
	C2_PRE(c2_rpc_packet_invariant(p) && bufvec != NULL);
	C2_PRE(!c2_rpc_packet_is_empty(p));

	if (C2_FI_ENABLED("fake_error"))
		return -EFAULT;

	bufvec_size = c2_vec_count(&bufvec->ov_vec);

	C2_ASSERT(C2_IS_8ALIGNED(bufvec_size));
	C2_ASSERT(c2_forall(i, bufvec->ov_vec.v_nr,
			    C2_IS_8ALIGNED(bufvec->ov_vec.v_count[i])));
	C2_ASSERT(bufvec_size >= p->rp_size);

	c2_bufvec_cursor_init(&cur, bufvec);
	C2_ASSERT(C2_IS_8ALIGNED(c2_bufvec_cursor_addr(&cur)));

	rc = c2_rpc_packet_encode_using_cursor(p, &cur);

	C2_RETURN(rc);
}

int c2_rpc_packet_encode_using_cursor(struct c2_rpc_packet    *packet,
				      struct c2_bufvec_cursor *cursor)
{
	struct c2_rpc_item *item;
	bool                end_of_bufvec;
	int                 rc;

	C2_ENTRY("packet: %p cursor: %p", packet, cursor);
	C2_PRE(c2_rpc_packet_invariant(packet) && cursor != NULL);
	C2_PRE(!c2_rpc_packet_is_empty(packet));

	rc = packet_header_encode(&packet->rp_ow, cursor);
	if (rc == 0) {
		for_each_item_in_packet(item, packet) {
			rc = item_encode(item, cursor);
			if (rc != 0)
				break;
		} end_for_each_item_in_packet;
	}
	end_of_bufvec = c2_bufvec_cursor_align(cursor, 8);
	C2_ASSERT(end_of_bufvec ||
		  C2_IS_8ALIGNED(c2_bufvec_cursor_addr(cursor)));
	C2_RETURN(rc);
}

static int packet_header_encode(struct c2_rpc_packet_onwire_header *ph,
				struct c2_bufvec_cursor            *cursor)

{
	struct c2_xcode_ctx ctx;
	int                 rc;

	C2_ENTRY();

	c2_xcode_ctx_init(&ctx, &PACKHD_XCODE_OBJ(ph));
	ctx.xcx_buf = *cursor;
	rc = c2_xcode_encode(&ctx);
	if (rc == 0)
		*cursor = ctx.xcx_buf;

	C2_RETURN(rc);
}

static int packet_header_decode(struct c2_bufvec_cursor            *cursor,
				struct c2_rpc_packet_onwire_header *ph)
{
	struct c2_xcode_ctx ctx;
	int                 rc;

	C2_ENTRY();
	C2_PRE(cursor != NULL && ph != NULL);

	c2_xcode_ctx_init(&ctx, &PACKHD_XCODE_OBJ(NULL));
	ctx.xcx_buf   = *cursor;
	ctx.xcx_alloc = c2_xcode_alloc;

	rc = c2_xcode_decode(&ctx);
	if (rc == 0) {
		*ph     = *(struct c2_rpc_packet_onwire_header *)
				c2_xcode_ctx_top(&ctx);
		*cursor = ctx.xcx_buf;
		c2_xcode_free(&PACKHD_XCODE_OBJ(c2_xcode_ctx_top(&ctx)));
	}

	C2_RETURN(rc);
}

static int item_encode(struct c2_rpc_item       *item,
		       struct c2_bufvec_cursor  *cursor)
{
	struct c2_rpc_item_onwire_header ioh;
	int                              rc;

	C2_ENTRY("item: %p cursor: %p", item, cursor);
	C2_PRE(item != NULL && cursor != NULL);
	C2_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_encode != NULL);

	ioh = (struct c2_rpc_item_onwire_header){
		.ioh_opcode = item->ri_type->rit_opcode,
		.ioh_magic  = C2_RPC_ITEM_MAGIC,
	};

	rc = c2_rpc_item_header_encode(&ioh, cursor);
	if (rc == 0)
		rc = item->ri_type->rit_ops->rito_encode(item->ri_type,
							 item, cursor);
	C2_RETURN(rc);
}

int c2_rpc_packet_decode(struct c2_rpc_packet *p,
			 struct c2_bufvec     *bufvec,
			 c2_bindex_t           off,
			 c2_bcount_t           len)
{
	struct c2_bufvec_cursor cursor;
	int                     rc;

	C2_ENTRY();
	C2_PRE(c2_rpc_packet_invariant(p) && bufvec != NULL && len > 0);
	C2_PRE(len <= c2_vec_count(&bufvec->ov_vec));
	C2_PRE(C2_IS_8ALIGNED(off) && C2_IS_8ALIGNED(len));
	C2_ASSERT(c2_forall(i, bufvec->ov_vec.v_nr,
			    C2_IS_8ALIGNED(bufvec->ov_vec.v_count[i])));

	c2_bufvec_cursor_init(&cursor, bufvec);
	c2_bufvec_cursor_move(&cursor, off);
	C2_ASSERT(C2_IS_8ALIGNED(c2_bufvec_cursor_addr(&cursor)));
	rc = c2_rpc_packet_decode_using_cursor(p, &cursor, len);
	C2_ASSERT(c2_bufvec_cursor_move(&cursor, 0) ||
		  C2_IS_8ALIGNED(c2_bufvec_cursor_addr(&cursor)));
	C2_RETURN(rc);
}

int c2_rpc_packet_decode_using_cursor(struct c2_rpc_packet    *p,
				      struct c2_bufvec_cursor *cursor,
				      c2_bcount_t              len)
{
	struct c2_rpc_packet_onwire_header poh;
	struct c2_rpc_item                *item;
	int                                rc;
	int                                i;

	C2_ENTRY();
	C2_PRE(c2_rpc_packet_invariant(p) && cursor != NULL);

	rc = packet_header_decode(cursor, &poh);
	if (rc != 0)
		C2_RETURN(rc);
	if (poh.poh_version != C2_RPC_VERSION_1 || poh.poh_nr_items == 0 ||
	    poh.poh_magic != C2_RPC_PACKET_HEAD_MAGIC)
		C2_RETURN(-EPROTO);

	for (i = 0; i < poh.poh_nr_items; ++i) {
		rc = item_decode(cursor, &item);
		if (rc != 0)
			C2_RETURN(rc);
		c2_rpc_packet_add_item(p, item);
		item = NULL;
	}
	c2_bufvec_cursor_align(cursor, 8);
	C2_ASSERT(c2_rpc_packet_invariant(p));

	C2_RETURN(0);
}

static int item_decode(struct c2_bufvec_cursor  *cursor,
		       struct c2_rpc_item      **item_out)
{
	struct c2_rpc_item_type         *item_type;
	struct c2_rpc_item_onwire_header ioh;
	int                              rc;

	C2_ENTRY();
	C2_PRE(cursor != NULL && item_out != NULL);

	rc = c2_rpc_item_header_decode(cursor, &ioh);
	if (rc != 0)
		C2_RETURN(rc);

	if (ioh.ioh_magic != C2_RPC_ITEM_MAGIC)
		return -EPROTO;

	*item_out = NULL;

	item_type = c2_rpc_item_type_lookup(ioh.ioh_opcode);
	if (item_type == NULL)
		C2_RETURN(-EPROTO);

	C2_ASSERT(item_type->rit_ops != NULL &&
		  item_type->rit_ops->rito_decode != NULL);

	return item_type->rit_ops->rito_decode(item_type, item_out, cursor);
}

void c2_rpc_packet_traverse_items(struct c2_rpc_packet *p,
				  item_visit_fn        *visit,
				  unsigned long         opaque_data)
{
	struct c2_rpc_item *item;

	C2_ENTRY("p: %p visit: %p", p, visit);
	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_LOG(C2_DEBUG, "nr_items: %u", (unsigned int)p->rp_ow.poh_nr_items);

	for_each_item_in_packet(item, p) {
		visit(item, opaque_data);
	} end_for_each_item_in_packet;

	C2_ASSERT(c2_rpc_packet_invariant(p));
	C2_LEAVE();
}

#undef C2_TRACE_SUBSYSTEM

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
