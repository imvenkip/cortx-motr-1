/* -*- C -*- */

/* This file is included into mem_xprt_xo.c */

/**
   @addtogroup bulkmem
   @{
 */

/**
   End point release subroutine invoked when the reference count goes
   to 0.
   Unlinks the end point from the domain, and releases the memory.
   Must be called holding the domain mutex.
*/
static void mem_xo_end_point_release(struct c2_ref *ref)
{
	struct c2_net_end_point *ep;
	struct c2_net_bulk_mem_end_point *mep;

	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	C2_PRE(c2_mutex_is_locked(&ep->nep_dom->nd_mutex));
	C2_PRE(mem_ep_invariant(ep));

	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	c2_list_del(&ep->nep_dom_linkage);
	ep->nep_dom = NULL;
	c2_free(mep);
}

/**
   Internal implementation of mem_xo_end_point_create().
 */
static int mem_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_domain *dom,
			 struct sockaddr_in *sa)
{
	C2_PRE(mem_dom_invariant(dom));

	struct c2_net_end_point *ep;
	struct c2_net_bulk_mem_end_point *mep;

	/* check if its already on the domain list */
	c2_list_for_each_entry(&dom->nd_end_points, ep,
			       struct c2_net_end_point,
			       nep_dom_linkage) {
		C2_ASSERT(mem_ep_invariant(ep));
		mep = container_of(ep,struct c2_net_bulk_mem_end_point,xep_ep);
		if (mep->xep_sa.sin_addr.s_addr == sa->sin_addr.s_addr &&
		    mep->xep_sa.sin_port == sa->sin_port ){
			c2_ref_get(&ep->nep_ref); /* refcnt++ */
			*epp = ep;
			return 0;
		}
	}

	/** allocate a new end point of appropriate size */
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	mep = c2_alloc(dp->xd_sizeof_ep);
	mep->xep_magic = C2_NET_XEP_MAGIC;
	mep->xep_sa.sin_addr = sa->sin_addr;
	mep->xep_sa.sin_port = sa->sin_port;
	ep = &mep->xep_ep;
	c2_ref_init(&ep->nep_ref, 1, mem_xo_end_point_release);
	ep->nep_dom = dom;
	c2_list_link_init(&ep->nep_dom_linkage);
	c2_list_add_tail(&dom->nd_end_points, &ep->nep_dom_linkage);
	C2_POST(mem_ep_invariant(ep));
	*epp = ep;
	return 0;
}

/**
   Compare two end points for equality.
 */
static bool mem_ep_is_equal(struct c2_net_end_point *ep1,
			    struct c2_net_end_point *ep2)
{
	C2_ASSERT(ep1 != NULL && ep2 != NULL);
	C2_ASSERT(mem_ep_invariant(ep1));
	C2_ASSERT(mem_ep_invariant(ep2));
	if (ep1 == ep2)
		return true;

	struct c2_net_bulk_mem_end_point *mep1;
	mep1 = container_of(ep1, struct c2_net_bulk_mem_end_point, xep_ep);
	struct c2_net_bulk_mem_end_point *mep2;
	mep2 = container_of(ep2, struct c2_net_bulk_mem_end_point, xep_ep);
	if (mep1->xep_sa.sin_addr.s_addr == mep2->xep_sa.sin_addr.s_addr &&
	    mep1->xep_sa.sin_port == mep2->xep_sa.sin_port)
		return true;
	return false;
}

/**
   Create a network buffer descriptor from an in-memory end point.
 */
static int mem_ep_create_desc(struct c2_net_end_point *ep,
			      struct c2_net_buf_desc *desc)
{
	C2_PRE(mem_ep_invariant(ep));
	desc->nbd_len = sizeof(struct sockaddr_in);
	desc->nbd_data = c2_alloc(desc->nbd_len);
	if (desc->nbd_data == NULL) {
		desc->nbd_len = 0;
		return -ENOMEM;
	}
	struct c2_net_bulk_mem_end_point *mep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	memcpy(desc->nbd_data, &mep->xep_sa, desc->nbd_len);
	return 0;
}

/**
   @} bulkmem
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
