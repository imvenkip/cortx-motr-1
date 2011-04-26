/* -*- C -*- */

/* This file is included by sunrpc_xprt_xo.c */

/**
   @addtogroup bulksunrpc
   @{
 */

/**
   Create a network buffer descriptor from a sunrpc end point.
   The descriptor is XDR encoded and returned as opaque data.

   @param desc Returns the descriptor
   @param ep Remote end point allowed active access
   @param tm Transfer machine holding the passive buffer
   @param qt The queue type
   @param buflen The amount data to transfer.
   @param buf_id The buffer identifier.
   @retval 0 success
   @retval -errno failure
 */
static int sunrpc_desc_create(struct c2_net_buf_desc *desc,
			      struct c2_net_end_point *ep,
			      struct c2_net_transfer_mc *tm,
			      enum c2_net_queue_type qt,
			      c2_bcount_t buflen,
			      int64_t buf_id)
{
	struct sunrpc_buf_desc sd = {
	    .sbd_id    = buf_id,
	    .sbd_qtype = qt,
	    .sbd_total = buflen
	};

	desc->nbd_len = sizeof(sd);
	desc->nbd_data = c2_alloc(desc->nbd_len);
	if (desc->nbd_data == NULL)
	    return -ENOMEM;

	XDR xdrs;
	xdrmem_create(&xdrs, desc->nbd_data, desc->nbd_len, XDR_ENCODE);
	C2_ASSERT(sunrpc_buf_desc_memlayout.fm_uxdr(&xdrs, &sd));
	xdr_destroy(&xdrs);
	return 0;
}

/**
   Decodes a network buffer descriptor.
   @param desc Network buffer descriptor pointer.
   @param sd Returns the descriptor contents.
   @retval 0 On success
   @retval -EINVAL Invalid transfer descriptor
 */
static int sunrpc_desc_decode(struct c2_net_buf_desc *desc,
			      struct sunrpc_buf_desc *sd)
{
	XDR xdrs;
	xdrmem_create(&xdrs, desc->nbd_data, desc->nbd_len, XDR_DECODE);
	C2_ASSERT(sunrpc_buf_desc_memlayout.fm_uxdr(&xdrs, &sd));
	xdr_destroy(&xdrs);
	return 0;
}

/**
   @} bulksunrpc
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
