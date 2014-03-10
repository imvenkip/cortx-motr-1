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

M0_INTERNAL int m0_rpc_item_header_encdec(struct m0_rpc_item_onwire_header *ioh,
					  struct m0_bufvec_cursor *cur,
					  enum m0_xcode_what what)
{
	M0_ENTRY("item header: %p", ioh);
	return M0_RCN(m0_xcode_encdec(&ITEM_HEAD_XCODE_OBJ(ioh), cur, what));
}

static int slot_ref_encdec(struct m0_rpc_onwire_slot_ref *osr,
			   struct m0_bufvec_cursor       *cur,
			   enum m0_xcode_what             what)
{
	return M0_RCN(m0_xcode_encdec(&SLOT_REF_XCODE_OBJ(osr), cur, what));
}


M0_INTERNAL int m0_rpc_slot_refs_encdec(struct m0_bufvec_cursor *cur,
					struct m0_rpc_slot_ref *slot_refs,
					int nr_slot_refs,
					enum m0_xcode_what what)
{
	int i;
	int rc = 0;

	M0_ENTRY();
	M0_PRE(slot_refs != NULL);
	M0_PRE(cur != NULL);

	for (i = 0; i < nr_slot_refs; ++i) {
		rc = slot_ref_encdec(&slot_refs[i].sr_ow, cur, what);
		if (rc != 0)
			break;
	}
	return M0_RCN(rc);
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
