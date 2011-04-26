/* -*- C -*- */

/**
   @addtogroup bulksunrpc
   @{
 */

int sunrpc_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return -ENOSYS;
}

int sunrpc_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return -ENOSYS;
}

static void sunrpc_wf_active_bulk(struct c2_net_transfer_mc *tm,
				  struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer *nb = NULL;

	int rc;
	struct sunrpc_buf_desc sd;
	rc = sunrpc_desc_decode(&nb->nb_desc, &sd);
	C2_ASSERT(rc == 0);
	C2_IMPOSSIBLE("todo: sunrpc_wf_active_bulk");
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
