/* -*- C -*- */

/* This file is included by sunrpc_xprt_xo.c */

/**
   @addtogroup bulksunrpc
   @{
 */

static int sunrpc_ep_mutex_initialized = 0;
static struct c2_mutex sunrpc_ep_mutex;


/**
   End point release subroutine for sunrpc.
   It corresponds to the base transports mem_xo_end_point_release
   subroutine.

   It releases the sunrpc related data structures, and then invokes
   the underlying base transport's release function.
 */
static void sunrpc_xo_end_point_release(struct c2_ref *ref)
{
	struct c2_net_end_point *ep;
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	struct c2_net_bulk_sunrpc_domain_pvt *dp;

	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	C2_PRE(c2_mutex_is_locked(&ep->nep_dom->nd_mutex));
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);
	C2_PRE(sunrpc_ep_invariant(ep));
	dp = ep->nep_dom->nd_xprt_private;

	/* sunrpc_ep_mutex not needed as this is during release */

	/* free the conn and sid */
	if (sep->xep_conn_valid) {
		c2_net_conn_release(sep->xep_conn);
	}
	if (sep->xep_sid_valid) {
		c2_service_id_fini(&sep->xep_sid);
	}
	sep->xep_magic = 0;

	/* release the end point with the base method */
	(*dp->xd_base_ops.bmo_ep_release)(ref);
}

/**
   Internal subroutine to create an end point. It corresponds to and
   replaces the base mem_ep_create subroutine.

   It sets the magic number, builds the service id and replaces the
   base transport release subroutine.

   @param ep  The newly created end point.
   @param dom The domain pointer.
   @retval 0 on success
   @retval -errno on failure.
 */
static int sunrpc_ep_create(struct c2_net_end_point **epp,
			    struct c2_net_domain *dom,
			    struct sockaddr_in *sa)
{
	int rc = 0;
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	struct c2_net_bulk_sunrpc_domain_pvt *dp = dom->nd_xprt_private;

	C2_PRE(sunrpc_dom_invariant(dom));
	rc = (*dp->xd_base_ops.bmo_ep_create)(epp, dom, sa);
	if (rc != 0)
		return rc;

	c2_mutex_lock(&sunrpc_ep_mutex);
	mep = container_of(*epp, struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);
	sep->xep_magic = C2_NET_BULK_SUNRPC_XEP_MAGIC;
	/* create the sid */
	if (!sep->xep_sid_valid) {
		char host[C2_NET_BULK_MEM_XEP_ADDR_LEN];
		char *p;
		/* copy the printable addr ("dotted_ip_addr:port") */
		strncpy(host, (*epp)->nep_addr, sizeof(host)-1);
		host[sizeof(host)-1] = '\0';
		for (p=host; *p && *p != ':'; p++);
		*p = '\0'; /* isolate the hostname */
		rc = c2_service_id_init(&sep->xep_sid, &dp->xd_rpc_dom,
					host,
					ntohs(mep->xep_sa.sin_port));
		if (rc == 0)
			sep->xep_sid_valid = true;
	}
	c2_mutex_unlock(&sunrpc_ep_mutex);
	if (rc == 0)
		C2_POST(sunrpc_ep_invariant(*epp));
	else {
		/* directly release the ep (we're in the dom mutex) */
		c2_ref_put(&(*epp)->nep_ref);
		*epp = NULL;
	}
	return rc;
}

#if 0
/**
   This subroutine ensures that the xep_conn pointer in the end point
   points to a network connection.
   @retval 0 Success, xep_conn can be used.
   @retval -errno Failure
 */
static int sunrpc_ep_make_conn(struct c2_net_end_point *ep)
{
	int rc = 0;
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);

	C2_PRE(sunrpc_ep_invariant(ep));
	C2_PRE(sep->xep_sid_valid);
	if (sep->xep_conn_valid) {
		C2_ASSERT(sep->xep_conn != NULL);
		return 0;
	}
	/* open a connection */
	c2_mutex_lock(&sunrpc_ep_mutex);
	do {
		if (sep->xep_conn_valid)
			break;
		rc = c2_net_conn_create(&sep->xep_sid);
		if (rc != 0)
			break;
		sep->xep_conn = c2_net_conn_find(&sep->xep_sid);
		if (sep->xep_conn == NULL) {
			rc = -ECONNRESET;
			break;
		}
		sep->xep_conn_valid = true;
		rc = 0;
	} while(0);
	c2_mutex_unlock(&sunrpc_ep_mutex);
	C2_POST(ergo(rc == 0, sep->xep_conn != NULL));
	return rc;
}
#endif

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
