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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 06/25/2011
 */

#include "lib/errno.h"
#include "rpc/rpc_onwire.h"

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			   struct c2_rpc_slot_ref *slot_ref,
			   enum c2_bufvec_what what);
static int sender_uuid_encdec(struct c2_bufvec_cursor *cur,
			      struct c2_rpc_sender_uuid *uuid,
			      enum c2_bufvec_what what);

/**
    Encodes/decodes the rpc item header into a bufvec
    @param cur Current bufvec cursor position
    @param item RPC item for which the header is to be encoded/decoded
    @param what Denotes type of operation (Encode or Decode)
    @retval 0 (success)
    @retval -errno  (failure)
*/
int c2_rpc_item_header_encdec(struct c2_rpc_item      *item,
			      struct c2_bufvec_cursor *cur,
			      enum c2_bufvec_what      what)
{
	uint64_t		 len;
	int			 rc;
	struct c2_rpc_item_type *item_type;

	C2_PRE(cur != NULL);
	C2_PRE(item != NULL);

	item_type = item->ri_type;
	if (what == C2_BUFVEC_ENCODE) {
		C2_ASSERT(item_type->rit_ops != NULL);
		//C2_ASSERT(item_type->rit_ops->rito_payload_size != NULL);
		len = c2_rpc_item_size(item);
	}
	rc = c2_bufvec_uint64(cur, &len, what) ?:
	     slot_ref_encdec(cur, item->ri_slot_refs, what);
	return rc;
}

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			   struct c2_rpc_slot_ref *slot_ref,
			   enum c2_bufvec_what what)
{
	struct c2_rpc_slot_ref    *sref;
	int			   rc;
	int			   slot_ref_cnt;
	int			   i;

	C2_PRE(slot_ref != NULL);
	C2_PRE(cur != NULL);

	/* Currently MAX slot references in sessions is 1. */
	slot_ref_cnt = 1;
	for (i = 0; i < slot_ref_cnt; ++i) {
		sref = &slot_ref[i];
		rc = c2_bufvec_uint64(cur, &sref->sr_verno.vn_lsn, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_sender_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_session_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_verno.vn_vc, what) ?:
		sender_uuid_encdec(cur, &sref->sr_uuid, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_persistent_verno.vn_lsn,
				 what) ?:
		c2_bufvec_uint64(cur,&sref->sr_last_persistent_verno.vn_vc,
				 what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_lsn, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_last_seen_verno.vn_vc, what) ?:
		c2_bufvec_uint32(cur, &sref->sr_slot_id, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_xid, what) ?:
		c2_bufvec_uint64(cur, &sref->sr_slot_gen, what);
		if (rc != 0)
			return -EFAULT;
	}
	return rc;
}

#if 0

/**
  Adds padding bytes to the c2_bufvec_cursor to keep it aligned at 8 byte
  boundaries.
*/
static int zero_padding_add(struct c2_bufvec_cursor *cur, uint64_t pad_bytes)
{
	uint64_t pad = 0;

	C2_PRE(cur != NULL);
	C2_PRE(pad_bytes <= sizeof pad);

	return c2_data_to_bufvec_copy(cur, &pad, pad_bytes);
}

/**
   Helper function used by encode/decode ops of each item type (rito_encode,
   rito_decode) for decoding an rpc item into/from a bufvec
*/
int item_encdec(struct c2_bufvec_cursor *cur, struct c2_rpc_item *item,
		enum c2_bufvec_what what)
{
	int                  rc;
	size_t               item_size;
	size_t               padding;
	struct c2_fop       *fop;
	struct c2_xcode_ctx  xc_ctx;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);

	rc = item_header_encdec(cur, item, what);
	if(rc != 0)
		return rc;

	if (what == C2_BUFVEC_ENCODE) {
		c2_xcode_ctx_init(&xc_ctx, &(struct c2_xcode_obj) {
				  fop->f_type->ft_xt,
				  c2_fop_data(fop)});
		xc_ctx.xcx_buf = *cur;
		rc = c2_xcode_encode(&xc_ctx);
		*cur = xc_ctx.xcx_buf;
	} else {
		c2_xcode_ctx_init(&xc_ctx, &(struct c2_xcode_obj) {
				  fop->f_type->ft_xt,
				  NULL});
		xc_ctx.xcx_alloc = c2_xcode_alloc;
		xc_ctx.xcx_buf   = *cur;
		rc = c2_xcode_decode(&xc_ctx);
		*cur = xc_ctx.xcx_buf;
		if (rc == 0) {
			fop->f_data.fd_data =
				xc_ctx.xcx_it.xcu_stack[0].s_obj.xo_ptr;
		}
	}

	/* Pad the message in buffer to 8 byte boundary */
	if (rc == 0) {
		c2_xcode_ctx_init(&xc_ctx, &(struct c2_xcode_obj) {
				  fop->f_type->ft_xt,
				  c2_fop_data(fop)});
		item_size = c2_xcode_length(&xc_ctx);
		padding   = c2_rpc_pad_bytes_get(item_size);
		rc = zero_padding_add(cur, padding);
	}

	return rc;
}
#endif

/** Helper functions to serialize uuid and slot references in rpc item header
    see rpc/rpc2.h */
static int sender_uuid_encdec(struct c2_bufvec_cursor *cur,
			      struct c2_rpc_sender_uuid *uuid,
			      enum c2_bufvec_what what)
{
	return c2_bufvec_uint64(cur, &uuid->su_uuid, what);
}

#if 0
/**
   Returns no of padding bytes that would be needed to keep a cursor aligned
   at 8 byte boundary.
*/
int c2_rpc_pad_bytes_get(const size_t size)
{
	return c2_round_up(size, BYTES_PER_XCODE_UNIT) - size;
}
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
