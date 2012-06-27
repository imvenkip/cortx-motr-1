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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 12/21/2011
 */

/**
   @addtogroup LNetXODFS
   @{
 */

/**
   End point release subroutine invoked when the reference count goes to 0.
   Unlinks the end point from the TM, and releases the memory.
   Must be called holding the TM mutex.
 */
static void nlx_ep_release(struct c2_ref *ref)
{
	struct c2_net_end_point *ep;
	struct nlx_xo_ep *xep;

	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	C2_PRE(c2_mutex_is_locked(&ep->nep_tm->ntm_mutex) &&
	       nlx_ep_invariant(ep));
	xep = container_of(ep, struct nlx_xo_ep, xe_ep);

	c2_list_del(&ep->nep_tm_linkage);
	ep->nep_tm = NULL;
	c2_free(xep);
}

static int nlx_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_transfer_mc *tm,
			 const struct nlx_core_ep_addr *cepa)
{
	struct c2_net_end_point *ep;
	struct nlx_xo_domain *dp;
	struct nlx_xo_ep *xep;

	NLXDBG((struct nlx_xo_transfer_mc *)tm->ntm_xprt_private, 2,
	       nlx_print_core_ep_addr("nlx_ep_create", cepa));
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex) && nlx_tm_invariant(tm));
	dp = tm->ntm_dom->nd_xprt_private;

	/* check if its already on the TM end point list */
	c2_list_for_each_entry(&tm->ntm_end_points, ep,
			       struct c2_net_end_point,
			       nep_tm_linkage) {
		C2_ASSERT(nlx_ep_invariant(ep));
		xep = container_of(ep, struct nlx_xo_ep, xe_ep);
		if (nlx_core_ep_eq(&xep->xe_core, cepa)) {
			c2_net_end_point_get(ep);
			*epp = ep;
			return 0;
		}
	}

	C2_ALLOC_PTR_ADDB(xep, &tm->ntm_addb, &nlx_addb_loc);
	if (xep == NULL)
		return -ENOMEM;
	xep->xe_magic = C2_NET_LNET_XE_MAGIC;
	xep->xe_core = *cepa;
	nlx_core_ep_addr_encode(&dp->xd_core, cepa, xep->xe_addr);
	ep = &xep->xe_ep;
	c2_ref_init(&ep->nep_ref, 1, nlx_ep_release);
	ep->nep_tm = tm;
	c2_list_add_tail(&tm->ntm_end_points, &ep->nep_tm_linkage);
	ep->nep_addr = &xep->xe_addr[0];
	C2_POST(nlx_ep_invariant(ep));
	*epp = ep;
	return 0;
}

/** @} */ /* LNetXODFS */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
