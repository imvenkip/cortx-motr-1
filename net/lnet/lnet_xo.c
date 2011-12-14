/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 11/16/2011
 */

/**
   @addtogroup LNetXODFS
   @{
*/

static int nlx_xo_dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	struct nlx_core_domain *dp;

	C2_PRE(dom->nd_xprt_private == NULL);
	C2_PRE(xprt == &c2_net_lnet_xprt);
	C2_ALLOC_PTR(dp);
	if (dp == NULL)
		return -ENOMEM;
	dom->nd_xprt_private = dp;

	return nlx_core_dom_init(dom, dp);
}

static void nlx_xo_dom_fini(struct c2_net_domain *dom)
{
	struct nlx_core_domain *dp = dom->nd_xprt_private;

	C2_PRE(dp != NULL);
	nlx_core_dom_fini(dp);
}

static c2_bcount_t nlx_xo_get_max_buffer_size(const struct c2_net_domain *dom)
{
	struct nlx_core_domain *dp = dom->nd_xprt_private;

	C2_PRE(dp != NULL);
	return nlx_core_get_max_buffer_size(dp);
}

static c2_bcount_t nlx_xo_get_max_buffer_segment_size(const struct
						      c2_net_domain *dom)
{
	struct nlx_core_domain *dp = dom->nd_xprt_private;

	C2_PRE(dp != NULL);
	return nlx_core_get_max_buffer_segment_size(dp);
}

static int32_t nlx_xo_get_max_buffer_segments(const struct c2_net_domain *dom)
{
	struct nlx_core_domain *dp = dom->nd_xprt_private;

	C2_PRE(dp != NULL);
	return nlx_core_get_max_buffer_segments(dp);
}

static int nlx_xo_end_point_create(struct c2_net_end_point **epp,
				   struct c2_net_transfer_mc *tm,
				   const char *addr)
{
	return -ENOSYS;
}

static int nlx_xo_buf_register(struct c2_net_buffer *nb)
{
	struct nlx_core_domain *dp = nb->nb_dom->nd_xprt_private;
	struct nlx_core_buffer *lcbuf;

	C2_PRE(dp != NULL);
	C2_PRE(nb->nb_xprt_private == NULL);
	C2_ALLOC_PTR(lcbuf);
	nb->nb_xprt_private = lcbuf;

	return nlx_core_buf_register(dp, nb, lcbuf);
}

static void nlx_xo_buf_deregister(struct c2_net_buffer *nb)
{
	struct nlx_core_domain *dp = nb->nb_dom->nd_xprt_private;
	struct nlx_core_buffer *bp = nb->nb_xprt_private;

	C2_PRE(dp != NULL);
	C2_PRE(bp != NULL);

	nlx_core_buf_deregister(dp, bp);
}

static int nlx_xo_buf_add(struct c2_net_buffer *nb)
{
	struct nlx_core_transfer_mc *tp = nb->nb_tm->ntm_xprt_private;
	struct nlx_core_buffer *bp = nb->nb_xprt_private;
	int ret;

	C2_PRE(tp != NULL);
	C2_PRE(bp != NULL);

	switch (nb->nb_qtype) {
	case C2_NET_QT_MSG_RECV:
		ret = nlx_core_buf_msg_recv(tp, bp);
		break;
	case C2_NET_QT_MSG_SEND:
		ret = nlx_core_buf_msg_send(tp, bp);
		break;
	case C2_NET_QT_PASSIVE_BULK_RECV:
		nlx_core_buf_match_bits_set(tp, bp);
		ret = nlx_core_buf_passive_recv(tp, bp);
		break;
	case C2_NET_QT_PASSIVE_BULK_SEND:
		ret = nlx_core_buf_passive_send(tp, bp);
		break;
	case C2_NET_QT_ACTIVE_BULK_RECV:
		ret = nlx_core_buf_active_recv(tp, bp);
		break;
	case C2_NET_QT_ACTIVE_BULK_SEND:
		ret = nlx_core_buf_active_send(tp, bp);
		break;
	default:
		C2_IMPOSSIBLE("invalid queue type");
		break;
	}

	return ret;
}

static void nlx_xo_buf_del(struct c2_net_buffer *nb)
{
	struct nlx_core_transfer_mc *tp = nb->nb_tm->ntm_xprt_private;
	struct nlx_core_buffer *bp = nb->nb_xprt_private;

	C2_PRE(tp != NULL);
	C2_PRE(bp != NULL);
	nlx_core_buf_del(tp, bp);
}

static int nlx_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static void nlx_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
}

static int nlx_xo_tm_start(struct c2_net_transfer_mc *tm, const char *addr)
{
	struct nlx_core_domain *dp = tm->ntm_dom->nd_xprt_private;
	struct nlx_core_transfer_mc *tp = tm->ntm_xprt_private;
	struct nlx_xo_ep *ep;
	int ret;

	C2_PRE(tp != NULL);
	C2_PRE(dp != NULL);
	C2_ALLOC_PTR(ep);

	ret = nlx_core_ep_addr_decode(dp, addr, &ep->xe_core);
	if (ret != 0) {
		c2_free(ep);
		return ret;
	}

	return nlx_core_tm_start(tm, tp, &ep->xe_core);
}

static int nlx_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	struct nlx_core_transfer_mc *tp = tm->ntm_xprt_private;

	C2_PRE(tp != NULL);
	nlx_core_tm_stop(tp);
	return 0;
}

static int nlx_xo_tm_confine(struct c2_net_transfer_mc *tm,
			     const struct c2_bitmap *processors)
{
	return -ENOSYS;
}

static void nlx_xo_bev_deliver_all(struct c2_net_transfer_mc *tm)
{
	struct nlx_core_transfer_mc *tp = tm->ntm_xprt_private;
	struct nlx_core_buffer_event bev;

	C2_PRE(tp != NULL);
	while (nlx_core_buf_event_get(tp, &bev))
		/* deliver the event */ ;
}

static int nlx_xo_bev_deliver_sync(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static bool nlx_xo_bev_pending(struct c2_net_transfer_mc *tm)
{
	struct nlx_core_transfer_mc *tp = tm->ntm_xprt_private;

	C2_PRE(tp != NULL);
	return nlx_core_buf_event_wait(tp, 0) == 0;
}

static void nlx_xo_bev_notify(struct c2_net_transfer_mc *tm,
			      struct c2_chan *chan)
{
}

static const struct c2_net_xprt_ops nlx_xo_xprt_ops = {
	.xo_dom_init                    = nlx_xo_dom_init,
	.xo_dom_fini                    = nlx_xo_dom_fini,
	.xo_get_max_buffer_size         = nlx_xo_get_max_buffer_size,
	.xo_get_max_buffer_segment_size = nlx_xo_get_max_buffer_segment_size,
	.xo_get_max_buffer_segments     = nlx_xo_get_max_buffer_segments,
	.xo_end_point_create            = nlx_xo_end_point_create,
	.xo_buf_register                = nlx_xo_buf_register,
	.xo_buf_deregister              = nlx_xo_buf_deregister,
	.xo_buf_add                     = nlx_xo_buf_add,
	.xo_buf_del                     = nlx_xo_buf_del,
	.xo_tm_init                     = nlx_xo_tm_init,
	.xo_tm_fini                     = nlx_xo_tm_fini,
	.xo_tm_start                    = nlx_xo_tm_start,
	.xo_tm_stop                     = nlx_xo_tm_stop,
	.xo_tm_confine                  = nlx_xo_tm_confine,
	.xo_bev_deliver_all             = nlx_xo_bev_deliver_all,
	.xo_bev_deliver_sync            = nlx_xo_bev_deliver_sync,
	.xo_bev_pending                 = nlx_xo_bev_pending,
	.xo_bev_notify                  = nlx_xo_bev_notify,
};

/**
   @} LNetXODFS
*/

struct c2_net_xprt c2_net_lnet_xprt = {
	.nx_name = "lnet",
	.nx_ops  = &nlx_xo_xprt_ops
};
C2_EXPORTED(c2_net_lnet_xprt);

int c2_net_lnet_ep_addr_net_cmp(const char *addr1, const char *addr2)
{
	return false;
}
C2_EXPORTED(c2_net_lnet_ep_addr_net_cmp);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
