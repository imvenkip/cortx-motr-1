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
 * Original creation date: 25-Jun-2011
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"

#include "lib/errno.h"
#include "rpc/rpc_onwire.h"
#include "rpc/rpc_onwire_xc.h"
#include "rpc/item.h"          /* m0_rpc_slot_ref */
#include "rpc/rpc_helpers.h"
#include "xcode/xcode.h"       /* M0_XCODE_OBJ */

/**
 * @addtogroup rpc
 * @{
 */

#define ITEM_HEAD_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_rpc_item_onwire_header_xc, ptr)
#define SLOT_REF_XCODE_OBJ(ptr)  M0_XCODE_OBJ(m0_rpc_onwire_slot_ref_xc, ptr)

M0_INTERNAL int m0_rpc_item_header_encode(struct m0_rpc_item_onwire_header *ioh,
					  struct m0_bufvec_cursor *cur)
{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY("item header: %p", ioh);
	M0_PRE(cur != NULL);
	M0_PRE(ioh != NULL);

	m0_xcode_ctx_init(&ctx, &ITEM_HEAD_XCODE_OBJ(ioh));
	ctx.xcx_buf   = *cur;
	rc = m0_xcode_encode(&ctx);
	if (rc == 0)
		*cur = ctx.xcx_buf;
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_item_header_decode(struct m0_bufvec_cursor *cur,
					  struct m0_rpc_item_onwire_header *ioh)
{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY();
	M0_PRE(cur != NULL);
	M0_PRE(ioh != NULL);

	m0_xcode_ctx_init(&ctx, &ITEM_HEAD_XCODE_OBJ(NULL));
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = m0_xcode_alloc;
	rc = m0_xcode_decode(&ctx);
	if (rc == 0) {
		struct m0_rpc_item_onwire_header *ioh_decoded;

		ioh_decoded = m0_xcode_ctx_top(&ctx);
		*ioh = *ioh_decoded;
		m0_xcode_free(&ITEM_HEAD_XCODE_OBJ(ioh_decoded));
		*cur = ctx.xcx_buf;
	}
	M0_RETURN(rc);
}

static int slot_ref_encode(struct m0_rpc_onwire_slot_ref *osr,
			   struct m0_bufvec_cursor       *cur)

{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY();
	m0_xcode_ctx_init(&ctx, &SLOT_REF_XCODE_OBJ(osr));
	ctx.xcx_buf = *cur;
	rc = m0_xcode_encode(&ctx);
	*cur = ctx.xcx_buf;
	M0_RETURN(rc);
}

static int slot_ref_decode(struct m0_bufvec_cursor       *cur,
			   struct m0_rpc_onwire_slot_ref *osr)
{
	struct m0_xcode_ctx ctx;
	int                 rc;

	M0_ENTRY();
	m0_xcode_ctx_init(&ctx, &SLOT_REF_XCODE_OBJ(NULL));
	ctx.xcx_buf   = *cur;
	ctx.xcx_alloc = m0_xcode_alloc;
	rc = m0_xcode_decode(&ctx);
	if (rc == 0) {
		struct m0_rpc_onwire_slot_ref *osr_top = m0_xcode_ctx_top(&ctx);
		*osr = *osr_top;
		m0_xcode_free(&SLOT_REF_XCODE_OBJ(osr_top));
	}
	*cur = ctx.xcx_buf;
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_slot_refs_encdec(struct m0_bufvec_cursor *cur,
					struct m0_rpc_slot_ref *slot_refs,
					int nr_slot_refs,
					enum m0_bufvec_what what)
{
	int i;
	int rc = 0;

	M0_ENTRY();
	M0_PRE(slot_refs != NULL);
	M0_PRE(cur != NULL);

	for (i = 0; i < nr_slot_refs; ++i) {
		struct m0_rpc_onwire_slot_ref *x = &slot_refs[i].sr_ow;
		rc = what == M0_BUFVEC_ENCODE ?
			slot_ref_encode(x, cur) : slot_ref_decode(cur, x);
		if (rc != 0)
			break;
	}
	M0_RETURN(rc);
}

#undef SLOT_REF_XCODE_OBJ
#undef M0_TRACE_SUBSYSTEM

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
