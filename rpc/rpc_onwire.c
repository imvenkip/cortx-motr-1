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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "rpc/rpc_onwire_xc.h"
#include "rpc/rpc_helpers.h"

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			   struct c2_rpc_slot_ref *slot_ref,
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

	C2_ENTRY("item: %p", item);
	C2_PRE(cur != NULL);
	C2_PRE(item != NULL);

	item_type = item->ri_type;
	if (what == C2_BUFVEC_ENCODE) {
		len = c2_rpc_item_size(item);
		rc = c2_bufvec_cursor_copyto(cur, &len, sizeof len);
	} else
		rc = c2_bufvec_cursor_copyfrom(cur, &len, sizeof len);

	if (rc != sizeof len)
		return -EINVAL;

	rc = slot_ref_encdec(cur, item->ri_slot_refs, what);

	C2_RETURN(rc);
}

static int slot_ref_encdec(struct c2_bufvec_cursor *cur,
			   struct c2_rpc_slot_ref  *slot_ref,
			   enum c2_bufvec_what      what)
{
	struct c2_rpc_onwire_slot_ref *osr = NULL;
	struct c2_xcode_ctx            ctx;
	int                            rc;
	int                            slot_ref_cnt;
	int                            i;

	C2_ENTRY();
	C2_PRE(slot_ref != NULL);
	C2_PRE(cur != NULL);

	/* Currently MAX slot references in sessions is 1. */
	slot_ref_cnt = 1;
	for (i = 0; i < slot_ref_cnt; ++i) {

		if (what == C2_BUFVEC_ENCODE)
			osr = &slot_ref[i].sr_ow;
		c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){
					c2_rpc_onwire_slot_ref_xc,
					osr });
		ctx.xcx_buf   = *cur;
		ctx.xcx_alloc = c2_xcode_alloc;
		rc = what == C2_BUFVEC_ENCODE ? c2_xcode_encode(&ctx) :
						c2_xcode_decode(&ctx);
		if (rc != 0)
			break;
		else {
			if (what == C2_BUFVEC_DECODE) {
				slot_ref[i].sr_ow =
					*(struct c2_rpc_onwire_slot_ref *)
						c2_xcode_ctx_to_inmem_obj(&ctx);
				c2_xcode_free(&(struct c2_xcode_obj){
						c2_rpc_onwire_slot_ref_xc,
						osr});
			}
		}
		*cur = ctx.xcx_buf;
	}

	C2_RETURN(rc);
	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
