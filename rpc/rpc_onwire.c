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
#include "xcode/xcode.h" /* C2_XCODE_OBJ */

int c2_rpc_item_header_encode(struct c2_rpc_item_onwire_header *ioh,
			      struct c2_bufvec_cursor          *cur)
{
	struct c2_xcode_ctx ctx;
	int                 rc;

	C2_ENTRY("item: %p", item);
	C2_PRE(cur != NULL);
	C2_PRE(ioh != NULL);

	c2_xcode_ctx_init(&ctx,
			  &C2_XCODE_OBJ(c2_rpc_item_onwire_header_xc, ioh));
	ctx.xcx_buf   = *cur;
	rc = c2_xcode_encode(&ctx);
	if (rc == 0)
		*cur = ctx.xcx_buf;
	return rc;
}

int c2_rpc_item_header_decode(struct c2_rpc_item_onwire_header *ioh,
			      struct c2_bufvec_cursor          *cur)
{
	struct c2_rpc_item_onwire_header *ioh_decoded = NULL;
	struct c2_xcode_ctx               ctx;
	int                               rc;

	C2_PRE(cur != NULL);
	C2_PRE(ioh != NULL);

	c2_xcode_ctx_init(&ctx,
			  &C2_XCODE_OBJ(c2_rpc_item_onwire_header_xc,
					ioh_decoded));
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = c2_xcode_alloc;
	rc = c2_xcode_decode(&ctx);
	if (rc == 0) {
		*ioh = *(struct c2_rpc_item_onwire_header *)
			c2_xcode_ctx_top(&ctx);
		c2_xcode_free(&C2_XCODE_OBJ(c2_rpc_onwire_slot_ref_xc,
					    ioh_decoded));
		*cur = ctx.xcx_buf;
	}
	C2_RETURN(rc);
}

int c2_rpc_item_slot_ref_encdec(struct c2_bufvec_cursor *cur,
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
		c2_xcode_ctx_init(&ctx,
				  &C2_XCODE_OBJ(c2_rpc_onwire_slot_ref_xc,
						osr));
		ctx.xcx_buf   = *cur;
		ctx.xcx_alloc = c2_xcode_alloc;
		rc = what == C2_BUFVEC_ENCODE ? c2_xcode_encode(&ctx) :
						c2_xcode_decode(&ctx);
		if (rc != 0)
			break;
		else if (what == C2_BUFVEC_DECODE) {
			slot_ref[i].sr_ow =
				*(struct c2_rpc_onwire_slot_ref *)
				c2_xcode_ctx_top(&ctx);
			c2_xcode_free(&C2_XCODE_OBJ(c2_rpc_onwire_slot_ref_xc,
						    osr));
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
