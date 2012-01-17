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

#ifndef __COLIBRI_NET_LNET_PVT_H__
#define __COLIBRI_NET_LNET_PVT_H__

/* forward references to other static functions */
static bool nlx_tm_invariant(const struct c2_net_transfer_mc *tm);
static void nlx_tm_ev_worker(struct c2_net_transfer_mc *tm);
static bool nlx_ep_invariant(const struct c2_net_end_point *ep);
static int nlx_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_transfer_mc *tm,
			 struct nlx_core_ep_addr *cepa);
static bool nlx_xo_buffer_bufvec_invariant(const struct c2_net_buffer *nb);
static bool nlx_core_tm_is_locked(const struct nlx_core_transfer_mc *ctm);

/**
   Inline helper to get the Core EP address pointer from an end point.
 */
static inline
struct nlx_core_ep_addr *nlx_ep_to_core(struct c2_net_end_point *ep)
{
	struct nlx_xo_ep *xep;
	C2_PRE(nlx_ep_invariant(ep));
	xep = container_of(ep, struct nlx_xo_ep, xe_ep);
	return &xep->xe_core;
}

static inline struct nlx_xo_transfer_mc *
nlx_core_tm_to_xo_tm(struct nlx_core_transfer_mc *ctm)
{
	return container_of(ctm, struct nlx_xo_transfer_mc, xtm_core);
}


/* core private */

/**
   Subroutine to allocate a new buffer event structure initialized
   with the producer space self pointer set.
   This subroutine is defined separately for the kernel and user space.
   @param ctm Core transfer machine pointer.
   In the user space transport this must be initialized at least with the
   core device driver file descriptor.
   In kernel space this is not used.
   @param bevp Buffer event return pointer.
   @post bev_cqueue_bless(&bevp->cbe_tm_link) has been invoked.
   @see bev_cqueue_bless()
 */
static int nlx_core_new_blessed_bev(struct nlx_core_transfer_mc *ctm,
				    struct nlx_core_buffer_event **bevp);

#endif /* __COLIBRI_NET_LNET_PVT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
