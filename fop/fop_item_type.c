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
#include "rpc/rpc_onwire.h"
#include "xcode/bufvec_xcode.h"

c2_bcount_t c2_fop_item_type_default_onwire_size(const struct c2_rpc_item *item)
{
	c2_bcount_t      len;
	struct c2_fop	*fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	C2_ASSERT(fop->f_type != NULL);
	C2_ASSERT(fop->f_type->ft_ops != NULL);
	C2_ASSERT(fop->f_type->ft_ops->fto_size_get != NULL);
	len = fop->f_type->ft_ops->fto_size_get(fop);
	len += ITEM_ONWIRE_HEADER_SIZE;
	return len;
}

int c2_fop_item_type_default_encode(struct c2_rpc_item_type *item_type,
				    struct c2_rpc_item      *item,
				    struct c2_bufvec_cursor *cur)
{
	int			rc;
	uint32_t		opcode;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	item_type = item->ri_type;
	opcode = item_type->rit_opcode;
	rc = c2_bufvec_uint32(cur, &opcode, C2_BUFVEC_ENCODE);
	if(rc == 0)
		rc = item_encdec(cur, item, C2_BUFVEC_ENCODE);
	return rc;
}

int c2_fop_item_type_default_decode(struct c2_rpc_item_type *item_type,
			      struct c2_rpc_item **item,
			      struct c2_bufvec_cursor *cur)
{
	int			 rc;
	struct c2_fop		*fop;
	struct c2_fop_type	*ftype;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	ftype = c2_item_type_to_fop_type(item_type);
	C2_ASSERT(ftype != NULL);
	fop = c2_fop_alloc(ftype, NULL);
	if (fop == NULL)
		return -ENOMEM;
	*item = c2_fop_to_rpc_item(fop);
	C2_ASSERT(*item != NULL);
	rc = item_encdec(cur, *item, C2_BUFVEC_DECODE);
	if (rc != 0)
		c2_fop_free(fop);

	return rc;
}

/** Default rpc item type ops for fop item types */
const struct c2_rpc_item_type_ops c2_rpc_fop_default_item_type_ops = {
	.rito_encode = c2_fop_item_type_default_encode,
	.rito_decode = c2_fop_item_type_default_decode,
	.rito_item_size = c2_fop_item_type_default_onwire_size,
};
C2_EXPORTED(c2_rpc_fop_default_item_type_ops);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
