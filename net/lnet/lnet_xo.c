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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 11/16/2011
 */

/**
   @addtogroup LNetXODFS
   @{
*/

static bool nlx_dom_invariant(const struct c2_net_domain *dom)
{
	const struct nlx_xo_domain *dp = dom->nd_xprt_private;
	return dp != NULL && dp->xd_dom == dom;
}

static bool nlx_ep_invariant(const struct c2_net_end_point *ep)
{
	const struct nlx_xo_ep *xep = container_of(ep, struct nlx_xo_ep, xe_ep);
	return xep->xe_magic == C2_NET_LNET_XE_MAGIC &&
		xep->xe_ep.nep_addr == &xep->xe_addr[0];
}

static bool nlx_buffer_invariant(const struct c2_net_buffer *nb)
{
	const struct nlx_xo_buffer *bp = nb->nb_xprt_private;
	return bp != NULL && bp->xb_nb == nb && nlx_dom_invariant(nb->nb_dom);
}

static bool nlx_tm_invariant(const struct c2_net_transfer_mc *tm)
{
	const struct nlx_xo_transfer_mc *tp = tm->ntm_xprt_private;
	return tp != NULL && tp->xtm_tm == tm && nlx_dom_invariant(tm->ntm_dom);
}

static int nlx_xo_dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	struct nlx_xo_domain *dp;
	int rc;

	C2_PRE(dom->nd_xprt_private == NULL);
	C2_PRE(xprt == &c2_net_lnet_xprt);
	C2_ALLOC_PTR(dp);
	if (dp == NULL)
		return -ENOMEM;
	dom->nd_xprt_private = dp;
	dp->xd_dom = dom;

	rc = nlx_core_dom_init(dom, &dp->xd_core);
	C2_POST(ergo(rc == 0, nlx_dom_invariant(dom)));
	return rc;
}

static void nlx_xo_dom_fini(struct c2_net_domain *dom)
{
	struct nlx_xo_domain *dp = dom->nd_xprt_private;

	C2_PRE(nlx_dom_invariant(dom));
	nlx_core_dom_fini(&dp->xd_core);
	c2_free(dp);
	dom->nd_xprt_private = NULL;
}

static c2_bcount_t nlx_xo_get_max_buffer_size(const struct c2_net_domain *dom)
{
	struct nlx_xo_domain *dp = dom->nd_xprt_private;

	C2_PRE(nlx_dom_invariant(dom));
	return nlx_core_get_max_buffer_size(&dp->xd_core);
}

static c2_bcount_t nlx_xo_get_max_buffer_segment_size(const struct
						      c2_net_domain *dom)
{
	struct nlx_xo_domain *dp = dom->nd_xprt_private;

	C2_PRE(nlx_dom_invariant(dom));
	return nlx_core_get_max_buffer_segment_size(&dp->xd_core);
}

static int32_t nlx_xo_get_max_buffer_segments(const struct c2_net_domain *dom)
{
	struct nlx_xo_domain *dp = dom->nd_xprt_private;

	C2_PRE(nlx_dom_invariant(dom));
	return nlx_core_get_max_buffer_segments(&dp->xd_core);
}

static int nlx_xo_end_point_create(struct c2_net_end_point **epp,
				   struct c2_net_transfer_mc *tm,
				   const char *addr)
{
	struct nlx_xo_domain *dp;
	struct nlx_core_ep_addr cepa;
	int rc;

	dp = tm->ntm_dom->nd_xprt_private;
	rc = nlx_core_ep_addr_decode(&dp->xd_core, addr, &cepa);
	if (rc != 0)
		return rc;
	if (cepa.cepa_tmid == C2_NET_LNET_TMID_INVALID)
		return -EINVAL;

	return nlx_ep_create(epp, tm, &cepa);
}

static int nlx_xo_buf_register(struct c2_net_buffer *nb)
{
	struct nlx_xo_domain *dp = nb->nb_dom->nd_xprt_private;
	struct nlx_xo_buffer *bp;
	int rc;

	C2_PRE(nb->nb_dom != NULL && nlx_dom_invariant(nb->nb_dom));
	C2_PRE(nb->nb_xprt_private == NULL);
	C2_ALLOC_PTR(bp);
	if (bp == NULL)
		return -ENOMEM;
	nb->nb_xprt_private = bp;
	bp->xb_nb = nb;

	rc = nlx_core_buf_register(&dp->xd_core, nb, &bp->xb_core);
	C2_POST(nlx_buffer_invariant(nb));
	return rc;
}

static void nlx_xo_buf_deregister(struct c2_net_buffer *nb)
{
	struct nlx_xo_domain *dp = nb->nb_dom->nd_xprt_private;
	struct nlx_xo_buffer *bp = nb->nb_xprt_private;

	C2_PRE(dp != NULL);
	C2_PRE(bp != NULL);

	nlx_core_buf_deregister(&dp->xd_core, &bp->xb_core);
}

static int nlx_xo_buf_add(struct c2_net_buffer *nb)
{
	struct nlx_xo_transfer_mc *tp = nb->nb_tm->ntm_xprt_private;
	struct nlx_xo_buffer *bp = nb->nb_xprt_private;
	struct nlx_core_transfer_mc *ctp;
	struct nlx_core_buffer *cbp;
	int rc;

	C2_PRE(tp != NULL);
	C2_PRE(bp != NULL);
	ctp = &tp->xtm_core;
	cbp = &bp->xb_core;

	switch (nb->nb_qtype) {
	case C2_NET_QT_MSG_RECV:
		rc = nlx_core_buf_msg_recv(ctp, cbp);
		break;
	case C2_NET_QT_MSG_SEND:
		rc = nlx_core_buf_msg_send(ctp, cbp);
		break;
	case C2_NET_QT_PASSIVE_BULK_RECV:
		nlx_core_buf_match_bits_set(ctp, cbp);
		rc = nlx_core_buf_passive_recv(ctp, cbp);
		break;
	case C2_NET_QT_PASSIVE_BULK_SEND:
		rc = nlx_core_buf_passive_send(ctp, cbp);
		break;
	case C2_NET_QT_ACTIVE_BULK_RECV:
		rc = nlx_core_buf_active_recv(ctp, cbp);
		break;
	case C2_NET_QT_ACTIVE_BULK_SEND:
		rc = nlx_core_buf_active_send(ctp, cbp);
		break;
	default:
		C2_IMPOSSIBLE("invalid queue type");
		break;
	}

	return rc;
}

static void nlx_xo_buf_del(struct c2_net_buffer *nb)
{
	struct nlx_xo_transfer_mc *tp = nb->nb_tm->ntm_xprt_private;
	struct nlx_xo_buffer *bp = nb->nb_xprt_private;

	C2_PRE(tp != NULL);
	C2_PRE(bp != NULL);
	nlx_core_buf_del(&tp->xtm_core, &bp->xb_core);
}

static int nlx_xo_tm_init(struct c2_net_transfer_mc *tm)
{
	struct nlx_xo_domain *dp;
	struct nlx_xo_transfer_mc *tp;
	int rc;

	C2_PRE(nlx_dom_invariant(tm->ntm_dom));
	C2_PRE(tm->ntm_xprt_private == NULL);

	dp = tm->ntm_dom->nd_xprt_private;
	C2_ALLOC_PTR(tp);
	if (tp == NULL)
		return -ENOMEM;
	tm->ntm_xprt_private = tp;

	/* defer init of thread and xtm_core to start time */
	rc = c2_bitmap_init(&tp->xtm_processors, 0);
	C2_ASSERT(rc == 0);
	c2_cond_init(&tp->xtm_ev_cond);

	tp->xtm_tm = tm;
	C2_POST(nlx_tm_invariant(tm));
	return 0;
}

static void nlx_xo_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct nlx_xo_transfer_mc *tp = tm->ntm_xprt_private;

	C2_PRE(nlx_tm_invariant(tm) && tp->xtm_busy == 0);

	if (tp->xtm_ev_thread.t_state != TS_PARKED)
		c2_thread_join(&tp->xtm_ev_thread);

	c2_bitmap_fini(&tp->xtm_processors);
	c2_cond_fini(&tp->xtm_ev_cond);
	tm->ntm_xprt_private = NULL;
	c2_free(tp);
}

static int nlx_xo_tm_start(struct c2_net_transfer_mc *tm, const char *addr)
{
	struct nlx_xo_domain *dp;
	struct nlx_xo_transfer_mc *tp;
	int rc;

	C2_PRE(nlx_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	dp = tm->ntm_dom->nd_xprt_private;
	tp = tm->ntm_xprt_private;

	rc = nlx_core_ep_addr_decode(&dp->xd_core, addr,
				     &tp->xtm_core.ctm_addr);
	if (rc != 0)
		return rc;

	rc = C2_THREAD_INIT(&tp->xtm_ev_thread,
			    struct c2_net_transfer_mc *, NULL,
			    &nlx_tm_ev_worker, tm,
			    "nlx_tm_ev_worker");
	return rc;
}

static int nlx_xo_tm_stop(struct c2_net_transfer_mc *tm, bool cancel)
{
	struct nlx_xo_transfer_mc *tp = tm->ntm_xprt_private;
	struct c2_net_buffer *nb;
	int qt;

	C2_PRE(nlx_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));

	/* walk through the queues and cancel every buffer if desired */
	if (cancel)
		for (qt = 0; qt < ARRAY_SIZE(tm->ntm_q); ++qt)
			c2_tlist_for(&tm_tl, &tm->ntm_q[qt], nb) {
				nlx_xo_buf_del(nb);
				/* bump the del stat count */
				tm->ntm_qstats[qt].nqs_num_dels++;
			} c2_tlist_endfor;

	c2_cond_signal(&tp->xtm_ev_cond, &tm->ntm_mutex);
	return 0;
}

static int nlx_xo_tm_confine(struct c2_net_transfer_mc *tm,
			     const struct c2_bitmap *processors)
{
	struct nlx_xo_transfer_mc *tp = tm->ntm_xprt_private;
	int rc;

	C2_PRE(nlx_tm_invariant(tm));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(processors != NULL);
	c2_bitmap_fini(&tp->xtm_processors);
	rc = c2_bitmap_init(&tp->xtm_processors, processors->b_nr);
	if (rc == 0)
		c2_bitmap_copy(&tp->xtm_processors, processors);
	return rc;
}

static void nlx_xo_bev_deliver_all(struct c2_net_transfer_mc *tm)
{
	struct nlx_xo_transfer_mc *tp = tm->ntm_xprt_private;
	struct nlx_core_buffer_event bev;

	C2_PRE(tp != NULL);
	while (nlx_core_buf_event_get(&tp->xtm_core, &bev))
		/* deliver the event */ ;
}

static int nlx_xo_bev_deliver_sync(struct c2_net_transfer_mc *tm)
{
	return -ENOSYS;
}

static bool nlx_xo_bev_pending(struct c2_net_transfer_mc *tm)
{
	struct nlx_xo_transfer_mc *tp = tm->ntm_xprt_private;

	C2_PRE(tp != NULL);
	return nlx_core_buf_event_wait(&tp->xtm_core, 0) == 0;
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

/**
   @addtogroup LNetDFS
   @{
*/

struct c2_net_xprt c2_net_lnet_xprt = {
	.nx_name = "lnet",
	.nx_ops  = &nlx_xo_xprt_ops
};
C2_EXPORTED(c2_net_lnet_xprt);

/**
   @} LNetDFS
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
