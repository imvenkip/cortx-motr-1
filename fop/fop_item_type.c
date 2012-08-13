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
#include "fop/fop_item_type.h"
#include "rpc/rpc_onwire.h"
#include "xcode/bufvec_xcode.h"

c2_bcount_t c2_fop_item_type_default_onwire_size(const struct c2_rpc_item *item)
{
	c2_bcount_t          len;
	struct c2_fop       *fop;
	struct c2_xcode_ctx  ctx;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	C2_ASSERT(fop != NULL);
	C2_ASSERT(fop->f_type != NULL);
	c2_xcode_ctx_init(&ctx, &C2_FOP_XCODE_OBJ(fop));
	len = c2_xcode_length(&ctx);
#if 0
	len += c2_rpc_pad_bytes_get(len) + ITEM_ONWIRE_HEADER_SIZE;
#endif
	//len += ITEM_ONWIRE_HEADER_SIZE;
	return len;
}

int c2_fop_item_type_default_encode(struct c2_rpc_item_type *item_type,
				    struct c2_rpc_item      *item,
				    struct c2_bufvec_cursor *cur)
{
	int	 rc;
	uint32_t opcode;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	item_type = item->ri_type;
	opcode = item_type->rit_opcode;
	rc = c2_bufvec_uint32(cur, &opcode, C2_BUFVEC_ENCODE) ?:
	     c2_fop_item_encdec(item, cur, C2_BUFVEC_ENCODE);

	return rc;
}

int c2_fop_item_type_default_decode(struct c2_rpc_item_type  *item_type,
				    struct c2_rpc_item      **item_out,
				    struct c2_bufvec_cursor  *cur)
{
	int			 rc;
	struct c2_fop		*fop;
	struct c2_fop_type	*ftype;
	struct c2_rpc_item      *item;

	C2_PRE(item_out != NULL);
	C2_PRE(cur != NULL);

	*item_out = NULL;
	ftype = c2_item_type_to_fop_type(item_type);
	C2_ASSERT(ftype != NULL);

	/*
	 * Decoding in xcode is different from sunrpc xdr where top object is
	 * allocated by caller; in xcode, even the top object is allocated,
	 * so we don't need to allocate the fop->f_data->fd_data.
	 */
	C2_ALLOC_PTR(fop);
	if (fop == NULL)
		return -ENOMEM;

	c2_fop_init(fop, ftype, NULL);
	item = c2_fop_to_rpc_item(fop);
	rc = c2_fop_item_encdec(item, cur, C2_BUFVEC_DECODE);
	if (rc == 0)
		*item_out = item;
	else
		c2_fop_free(fop);

	return rc;
}

#if 0
/**
   Helper function used by encode/decode ops of each item type (rito_encode,
   rito_decode) for decoding an rpc item into/from a bufvec
*/
int c2_fop_item_encdec(struct c2_rpc_item      *item,
		       struct c2_bufvec_cursor *cur,
		       enum c2_bufvec_what      what)
{
	int		 rc;
	struct	c2_fop	*fop;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	fop = c2_rpc_item_to_fop(item);
	rc = c2_rpc_item_header_encdec(item, cur, what) ?:
	     c2_xcode_bufvec_fop(cur, fop, what);
	return rc;
}

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

#else

static void *xcode_top_obj(struct c2_xcode_ctx *ctx)
{
	return ctx->xcx_it.xcu_stack[0].s_obj.xo_ptr;
}

/**
   Helper function used by encode/decode ops of each item type (rito_encode,
   rito_decode) for decoding an rpc item into/from a bufvec
*/
int c2_fop_item_encdec(struct c2_rpc_item      *item,
		       struct c2_bufvec_cursor *cur,
		       enum c2_bufvec_what      what)
{
	int                  rc;
	//size_t               item_size;
	//size_t               padding;
	struct c2_fop       *fop;
	struct c2_xcode_ctx  xc_ctx;

	C2_PRE(item != NULL);
	C2_PRE(cur != NULL);

	fop = c2_rpc_item_to_fop(item);

	rc = c2_rpc_item_header_encdec(item, cur, what);
	if(rc != 0)
		return rc;

	c2_xcode_ctx_init(&xc_ctx, &C2_FOP_XCODE_OBJ(fop));
	/* structure instance copy! */
	xc_ctx.xcx_buf   = *cur;
	xc_ctx.xcx_alloc = c2_xcode_alloc;

	rc = what == C2_BUFVEC_ENCODE ? c2_xcode_encode(&xc_ctx) :
					c2_xcode_decode(&xc_ctx);
	if (rc == 0) {
		if (what == C2_BUFVEC_DECODE)
			fop->f_data.fd_data = xcode_top_obj(&xc_ctx);
		*cur = xc_ctx.xcx_buf;
	}

#if 0
	/* Pad the message in buffer to 8 byte boundary */
	if (rc == 0) {
		c2_xcode_ctx_init(&xc_ctx, &C2_FOP_XCODE_OBJ(fop));
		item_size = c2_xcode_length(&xc_ctx);
		padding   = c2_rpc_pad_bytes_get(item_size);
		rc = zero_padding_add(cur, padding);
	}
#endif
	return rc;
}
#endif

/** Default rpc item type ops for fop item types */
const struct c2_rpc_item_type_ops c2_rpc_fop_default_item_type_ops = {
	.rito_encode       = c2_fop_item_type_default_encode,
	.rito_decode       = c2_fop_item_type_default_decode,
	.rito_payload_size = c2_fop_item_type_default_onwire_size,
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
