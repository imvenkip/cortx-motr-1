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

/**
 * @addtogroup rpc
 * @{
 */

#define ITEM_HEAD_XCODE_OBJ(ptr) C2_XCODE_OBJ(c2_rpc_item_onwire_header_xc, ptr)
#define SLOT_REF_XCODE_OBJ(ptr)  C2_XCODE_OBJ(c2_rpc_onwire_slot_ref_xc, ptr)

static int slot_ref_encode(struct c2_rpc_onwire_slot_ref *osr,
			   struct c2_bufvec_cursor       *cur);

static int slot_ref_decode(struct c2_bufvec_cursor       *cur,
			   struct c2_rpc_onwire_slot_ref *osr);

C2_INTERNAL int c2_rpc_item_header_encode(struct c2_rpc_item_onwire_header *ioh,
					  struct c2_bufvec_cursor *cur)
{
	struct c2_xcode_ctx ctx;
	int                 rc;

	C2_ENTRY("item header: %p", ioh);
	C2_PRE(cur != NULL);
	C2_PRE(ioh != NULL);

	c2_xcode_ctx_init(&ctx, &ITEM_HEAD_XCODE_OBJ(ioh));
	ctx.xcx_buf   = *cur;
	rc = c2_xcode_encode(&ctx);
	if (rc == 0)
		*cur = ctx.xcx_buf;
	C2_RETURN(rc);
}

C2_INTERNAL int c2_rpc_item_header_decode(struct c2_bufvec_cursor *cur,
					  struct c2_rpc_item_onwire_header *ioh)
{
	struct c2_xcode_ctx ctx;
	int                 rc;

	C2_ENTRY();
	C2_PRE(cur != NULL);
	C2_PRE(ioh != NULL);

	c2_xcode_ctx_init(&ctx, &ITEM_HEAD_XCODE_OBJ(NULL));
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = c2_xcode_alloc;
	rc = c2_xcode_decode(&ctx);
	if (rc == 0) {
		struct c2_rpc_item_onwire_header *ioh_decoded;

		ioh_decoded = c2_xcode_ctx_top(&ctx);
		*ioh = *ioh_decoded;
		c2_xcode_free(&ITEM_HEAD_XCODE_OBJ(ioh_decoded));
		*cur = ctx.xcx_buf;
	}
	C2_RETURN(rc);
}

C2_INTERNAL int c2_rpc_item_slot_ref_encdec(struct c2_bufvec_cursor *cur,
					    struct c2_rpc_slot_ref *slot_ref,
					    int nr_slot_refs,
					    enum c2_bufvec_what what)
{
	struct c2_rpc_onwire_slot_ref *osr = NULL;
	int                            rc;
	int                            i;

	C2_ENTRY();
	C2_PRE(slot_ref != NULL);
	C2_PRE(cur != NULL);

	for (i = 0, rc = 0; rc == 0 && i < nr_slot_refs; ++i) {
		osr = &slot_ref[i].sr_ow;
		rc = what == C2_BUFVEC_ENCODE ?
			slot_ref_encode(osr, cur) :
			slot_ref_decode(cur, osr);
	}

	C2_RETURN(rc);
}

static int slot_ref_encode(struct c2_rpc_onwire_slot_ref *osr,
			   struct c2_bufvec_cursor       *cur)

{
	struct c2_xcode_ctx ctx;
	int                 rc;

	C2_ENTRY();
	c2_xcode_ctx_init(&ctx, &SLOT_REF_XCODE_OBJ(osr));
	ctx.xcx_buf = *cur;
	rc = c2_xcode_encode(&ctx);
	*cur = ctx.xcx_buf;
	C2_RETURN(rc);
}

static int slot_ref_decode(struct c2_bufvec_cursor       *cur,
			   struct c2_rpc_onwire_slot_ref *osr)
{
	struct c2_xcode_ctx ctx;
	int                 rc;

	C2_ENTRY();
	c2_xcode_ctx_init(&ctx, &SLOT_REF_XCODE_OBJ(NULL));
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = c2_xcode_alloc;
	rc = c2_xcode_decode(&ctx);
	if (rc == 0) {
		struct c2_rpc_onwire_slot_ref *osr_top = c2_xcode_ctx_top(&ctx);
		*osr = *osr_top;
		c2_xcode_free(&SLOT_REF_XCODE_OBJ(osr_top));
	}
	*cur = ctx.xcx_buf;
	C2_RETURN(rc);
}

#undef C2_TRACE_SUBSYSTEM
#undef SLOT_REF_XCODE_OBJ

/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
