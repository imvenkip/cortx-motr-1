/* -*- C -*- */

/**
   @addtogroup bulksunrpc
   @{
 */

/**
 */
static void sunrpc_wf_msg_send(struct c2_net_transfer_mc *tm,
			       struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
	C2_PRE(nb != NULL &&
	       nb->nb_qtype == C2_NET_QT_MSG_SEND &&
	       nb->nb_tm == tm &&
	       nb->nb_ep != NULL);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

}

static int sunrpc_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return -ENOSYS;
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
