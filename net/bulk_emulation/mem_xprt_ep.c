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
			 struct sockaddr_in *sa,
			 uint32_t id)
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
		if (MEM_SA_EQ(&mep->xep_sa, sa) && mep->xep_service_id == id) {
			c2_ref_get(&ep->nep_ref);
			*epp = ep;
			return 0;
		}
	}

	/* allocate a new end point of appropriate size */
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	mep = c2_alloc(dp->xd_sizeof_ep);
	if (mep == NULL)
		return -ENOMEM;
	mep->xep_magic = C2_NET_BULK_MEM_XEP_MAGIC;
	mep->xep_sa.sin_addr = sa->sin_addr;
	mep->xep_sa.sin_port = sa->sin_port;
	mep->xep_service_id  = id;
	/* create the printable representation */
	{
		char dot_ip[17];
		int i;
		size_t len = 0;
		in_addr_t a = ntohl(sa->sin_addr.s_addr);
		int nib[4];
		for (i = 3; i >= 0; i--) {
			nib[i] = a & 0xff;
			a >>= 8;
		}
		for (i = 0; i < 4; ++i) {
			len += sprintf(&dot_ip[len], "%d.", nib[i]);
		}
		C2_ASSERT(len < sizeof(dot_ip));
		dot_ip[len-1] = '\0';
		if (id > 0)
			sprintf(mep->xep_addr, "%s:%d:%u", dot_ip,
				ntohs(sa->sin_port), id);
		else
			sprintf(mep->xep_addr, "%s:%d", dot_ip,
				ntohs(sa->sin_port));
		C2_ASSERT(strlen(mep->xep_addr) < C2_NET_BULK_MEM_XEP_ADDR_LEN);
	}
	ep = &mep->xep_ep;
	c2_ref_init(&ep->nep_ref, 1, dp->xd_ops.bmo_ep_release);
	ep->nep_dom = dom;
	c2_list_add_tail(&dom->nd_end_points, &ep->nep_dom_linkage);
	ep->nep_addr = &mep->xep_addr[0];
	C2_POST(mem_ep_invariant(ep));
	*epp = ep;
	return 0;
}

/**
   Compare an end point with a sockaddr_in for equality. The id field
   is not considered.
   @param ep End point
   @param sa sockaddr_in pointer
   @param true Match
   @param false Do not match
 */
static bool mem_ep_equals_addr(struct c2_net_end_point *ep,
			       struct sockaddr_in *sa)
{
	C2_ASSERT(mem_ep_invariant(ep));
	struct c2_net_bulk_mem_end_point *mep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);

	if (MEM_SA_EQ(&mep->xep_sa, sa))
		return true;
	return false;
}

/**
   Compare two end points for equality. Only the addresses are matched.
   @param ep1 First end point
   @param ep2 Second end point
   @param true Match
   @param false Do not match
 */
static bool mem_eps_are_equal(struct c2_net_end_point *ep1,
			      struct c2_net_end_point *ep2)
{
	C2_ASSERT(ep1 != NULL && ep2 != NULL);
	C2_ASSERT(mem_ep_invariant(ep1));
	if (ep1 == ep2)
		return true;

	struct c2_net_bulk_mem_end_point *mep1;
	mep1 = container_of(ep1, struct c2_net_bulk_mem_end_point, xep_ep);
	return mem_ep_equals_addr(ep2, &mep1->xep_sa);
}

/**
   Create a network buffer descriptor from an in-memory end point.

   The descriptor used by the in-memory transport is not encoded as it
   is never accessed out of the process.
   @param ep Remote end point allowed active access
   @param tm Transfer machine holding the passive buffer
   @param qt The queue type
   @param buflen The amount data to transfer.
   @param buf_id The buffer identifier.
   @param desc Returns the descriptor
 */
static int mem_desc_create(struct c2_net_buf_desc *desc,
			   struct c2_net_end_point *ep,
			   struct c2_net_transfer_mc *tm,
			   enum c2_net_queue_type qt,
			   c2_bcount_t buflen,
			   int64_t buf_id)
{
	C2_PRE(mem_ep_invariant(ep));
	struct mem_desc *md;

	desc->nbd_len = sizeof(*md);
	md = c2_alloc(desc->nbd_len);
	desc->nbd_data = (char *) md;
	if (desc->nbd_data == NULL) {
		desc->nbd_len = 0;
		return -ENOMEM;
	}

	struct c2_net_bulk_mem_end_point *mep;

	/* copy the active end point address */
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	md->md_active = mep->xep_sa;

	/* copy the passive end point address */
	mep = container_of(tm->ntm_ep,struct c2_net_bulk_mem_end_point,xep_ep);
	md->md_passive = mep->xep_sa;

	md->md_qt = qt;
	md->md_len = buflen;
	md->md_buf_id = buf_id;

	return 0;
}

/**
   Decodes a network buffer descriptor.
   @param desc Network buffer descriptor pointer.
   @param md Returns the descriptor contents. The pointer does not
   allocate memory but instead points to within the network buffer
   descriptor, so don't free it.
   @retval 0 On success
   @retval -EINVAL Invalid transfer descriptor
 */
static int mem_desc_decode(struct c2_net_buf_desc *desc,
			   struct mem_desc **p_md)
{
	if (desc->nbd_len != sizeof(**p_md) ||
	    desc->nbd_data == NULL)
		return -EINVAL;
	*p_md = (struct mem_desc *) desc->nbd_data;
	return 0;
}

/**
   Compares if two descriptors are equal.
 */
static bool mem_desc_equal(struct c2_net_buf_desc *d1,
			   struct c2_net_buf_desc *d2)
{
	/* could do a byte comparison too */
	struct mem_desc *md1;
	struct mem_desc *md2;
	int rc;
	rc = mem_desc_decode(d1, &md1);
	if (rc == 0)
		rc = mem_desc_decode(d2, &md2);
	if (rc != 0)
		return false;
	if (md1->md_buf_id == md2->md_buf_id &&
	    MEM_SA_EQ(&md1->md_active,  &md2->md_active) &&
	    MEM_SA_EQ(&md1->md_passive, &md2->md_passive))
		return true;
	return false;
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
