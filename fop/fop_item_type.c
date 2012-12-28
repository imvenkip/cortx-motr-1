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
 * Original creation date: 11/18/2011
 */

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fop.h"
#include "fop/fop_item_type.h"
#include "rpc/rpc_helpers.h"

M0_INTERNAL m0_bcount_t m0_fop_item_type_default_payload_size(const struct
							      m0_rpc_item *item)
{
	M0_PRE(item != NULL);

	return m0_fop_data_size(m0_rpc_item_to_fop(item));
}

M0_INTERNAL int m0_fop_item_type_default_encode(const struct m0_rpc_item_type
						*item_type,
						struct m0_rpc_item *item,
						struct m0_bufvec_cursor *cur)
{
	M0_PRE(item != NULL);
	M0_PRE(cur != NULL);

	return m0_fop_item_encdec(item, cur, M0_BUFVEC_ENCODE);
}

M0_INTERNAL int m0_fop_item_type_default_decode(const struct m0_rpc_item_type
						*item_type,
						struct m0_rpc_item **item_out,
						struct m0_bufvec_cursor *cur)
{
	int			 rc;
	struct m0_fop		*fop;
	struct m0_fop_type	*ftype;
	struct m0_rpc_item      *item;

	M0_PRE(item_out != NULL);
	M0_PRE(cur != NULL);

	*item_out = NULL;
	ftype = m0_item_type_to_fop_type(item_type);
	M0_ASSERT(ftype != NULL);

	/*
	 * Decoding in xcode is different from sunrpc xdr where top object is
	 * allocated by caller; in xcode, even the top object is allocated,
	 * so we don't need to allocate the fop->f_data->fd_data.
	 */
	M0_ALLOC_PTR(fop);
	if (fop == NULL)
		return -ENOMEM;

	m0_fop_init(fop, ftype, NULL, m0_fop_release);
	item = m0_fop_to_rpc_item(fop);
	rc = m0_fop_item_encdec(item, cur, M0_BUFVEC_DECODE);
	if (rc == 0)
		*item_out = item;
	else
		m0_fop_put(fop);

	return rc;
}

void m0_fop_item_get(struct m0_rpc_item *item)
{
	m0_fop_get(m0_rpc_item_to_fop(item));
}

void m0_fop_item_put(struct m0_rpc_item *item)
{
	m0_fop_put(m0_rpc_item_to_fop(item));
}

/**
   Helper function used by encode/decode ops of each item type (rito_encode,
   rito_decode) for decoding an rpc item into/from a bufvec
*/
M0_INTERNAL int m0_fop_item_encdec(struct m0_rpc_item *item,
				   struct m0_bufvec_cursor *cur,
				   enum m0_bufvec_what what)
{
	M0_PRE(item != NULL);
	M0_PRE(cur != NULL);

	/* Currently MAX slot references in sessions is 1. */
	return m0_rpc_item_slot_ref_encdec(cur, item->ri_slot_refs, 1, what) ?:
		m0_fop_encdec(m0_rpc_item_to_fop(item), cur, what);
}

/** Default rpc item type ops for fop item types */
const struct m0_rpc_item_type_ops m0_fop_default_item_type_ops = {
	M0_FOP_DEFAULT_ITEM_TYPE_OPS,
};
M0_EXPORTED(m0_fop_default_item_type_ops);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
