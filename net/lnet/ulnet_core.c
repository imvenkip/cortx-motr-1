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
 * Original creation date: 12/14/2011
 */

int nlx_core_dom_init(struct c2_net_domain *dom, struct nlx_core_domain *lcdom)
{
	/* XXX implement */
	return -ENOSYS;
}

void nlx_core_dom_fini(struct nlx_core_domain *lcdom)
{
	/* XXX implement */
}

c2_bcount_t nlx_core_get_max_buffer_size(struct nlx_core_domain *lcdom)
{
	/* XXX implement */
	return 0;
}

c2_bcount_t nlx_core_get_max_buffer_segment_size(struct nlx_core_domain *lcdom)
{
	/* XXX implement */
	return 0;
}

int32_t nlx_core_get_max_buffer_segments(struct nlx_core_domain *lcdom)
{
	/* XXX implement */
	return 0;
}

int nlx_core_buf_register(struct nlx_core_domain *lcdom,
			  nlx_core_opaque_ptr_t buffer_id,
			  const struct c2_bufvec *bvec,
			  struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
	C2_PRE(nlx_core_buffer_invariant(lcbuf));
	return -ENOSYS;
}

void nlx_core_buf_deregister(struct nlx_core_domain *lcdom,
			     struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
}

int nlx_core_buf_msg_recv(struct nlx_core_transfer_mc *lctm,
			  struct nlx_core_buffer *lcbuf)
{
	/* XXX temp: really gets called in kernel event cb */
	struct nlx_core_bev_link *ql;
	ql = bev_cqueue_pnext(&lctm->ctm_bevq);
	C2_ASSERT(ql != NULL);
	bev_cqueue_put(&lctm->ctm_bevq);

	/* XXX temp: just to compile in user space */
	nlx_core_bevq_provision(lctm, lcbuf->cb_max_receive_msgs);
	nlx_core_bevq_release(lctm, lcbuf->cb_max_receive_msgs);

	return -ENOSYS;
}

int nlx_core_buf_msg_send(struct nlx_core_transfer_mc *lctm,
			  struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_active_recv(struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_active_send(struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
	return -ENOSYS;
}

void nlx_core_buf_match_bits_set(struct nlx_core_transfer_mc *lctm,
				 struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
}

int nlx_core_buf_passive_recv(struct nlx_core_transfer_mc *lctm,
			      struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_passive_send(struct nlx_core_transfer_mc *lctm,
			      struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_del(struct nlx_core_transfer_mc *lctm,
		     struct nlx_core_buffer *lcbuf)
{
	/* XXX implement */
	return -ENOSYS;
}

int nlx_core_buf_event_wait(struct nlx_core_transfer_mc *lctm,
			    c2_time_t timeout)
{
	/* XXX implement */
	return -ENOSYS;
}

bool nlx_core_buf_event_get(struct nlx_core_transfer_mc *lctm,
			    struct nlx_core_buffer_event *lcbe)
{
	struct nlx_core_bev_link *link;
	struct nlx_core_buffer_event *bev;

	C2_PRE(lctm != NULL);
	C2_PRE(lcbe != NULL);

	/* XXX temp code to cause APIs to be used */
	if (!bev_cqueue_is_empty(&lctm->ctm_bevq)) {
		link = bev_cqueue_get(&lctm->ctm_bevq);
		if (link != NULL) {
			bev = container_of(link, struct nlx_core_buffer_event,
					   cbe_tm_link);
			*lcbe = *bev;
			C2_SET0(&lcbe->cbe_tm_link); /* copy is not in queue */
			return true;
		}
	}
	return false;
}

int nlx_core_ep_addr_decode(struct nlx_core_domain *lcdom,
			    const char *ep_addr,
			    struct nlx_core_ep_addr *cepa)
{
	/* XXX implement */
	return -ENOSYS;
}

void nlx_core_ep_addr_encode(struct nlx_core_domain *lcdom,
			     const struct nlx_core_ep_addr *cepa,
			     char buf[C2_NET_LNET_XEP_ADDR_LEN])
{
	/* XXX implement */
}

int nlx_core_nidstrs_get(char * const **nidary)
{
	/* XXX implement */
	return -ENOSYS;
}

void nlx_core_nidstrs_put(char * const **nidary)
{
	/* XXX implement */
}

int nlx_core_tm_start(struct c2_net_transfer_mc *tm,
		      struct nlx_core_transfer_mc *lctm,
		      struct nlx_core_ep_addr *cepa,
		      struct c2_net_end_point **epp)
{
	struct nlx_xo_domain *dp = tm->ntm_dom->nd_xprt_private;
	struct nlx_xo_ep *xep = container_of(cepa, struct nlx_xo_ep, xe_core);
	struct nlx_core_buffer_event *e1;
	struct nlx_core_buffer_event *e2;

	/* XXX: temp, really belongs in async and/or kernel code */
	C2_ALLOC_PTR(e1);
	e1->cbe_tm_link.cbl_c_self = (nlx_core_opaque_ptr_t) &e1->cbe_tm_link;
	bev_link_bless(&e1->cbe_tm_link);
	C2_ALLOC_PTR(e2);
	e2->cbe_tm_link.cbl_c_self = (nlx_core_opaque_ptr_t) &e2->cbe_tm_link;
	bev_link_bless(&e2->cbe_tm_link);
	bev_cqueue_init(&lctm->ctm_bevq, &e1->cbe_tm_link, &e2->cbe_tm_link);
	C2_ASSERT(bev_cqueue_size(&lctm->ctm_bevq) == 2);
	nlx_core_ep_addr_encode(&dp->xd_core, cepa, xep->xe_addr);

	C2_POST(nlx_core_tm_invariant(lctm));
	return -ENOSYS;
}

/* XXX duplicate code, see klnet_core.c, refactor during ulnet task */
static void nlx_core_bev_free_cb(struct nlx_core_bev_link *ql)
{
	struct nlx_core_buffer_event *bev;
	if (ql != NULL) {
		bev = container_of(ql, struct nlx_core_buffer_event,
				   cbe_tm_link);
		c2_free(bev);
	}
}

void nlx_core_tm_stop(struct nlx_core_transfer_mc *lctm)
{
	/* XXX: temp, really belongs in async code */
	bev_cqueue_fini(&lctm->ctm_bevq, nlx_core_bev_free_cb);
}

int nlx_core_new_blessed_bev(struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_buffer_event **bevp)
{
	return -ENOSYS;
}

static void nlx_core_fini(void)
{
}

static int nlx_core_init(void)
{
	return 0;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
