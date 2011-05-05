/* -*- C -*- */

/* This file is included by sunrpc_xprt_xo.c */

/**
   @addtogroup bulksunrpc
   @{
 */

/**
   The uuid of a service will be created by using the following sprintf
   format string and providing the port number as its argument.
   It has to be unique on a given node.
 */
static const char *c2_net_bulk_sunrpc_uuid_fmt = "BulkSunrpc-%d";

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

	/* free the conn and sid */
	if (sep->xep_conn_created) {
		struct c2_net_conn *conn = c2_net_conn_find(&sep->xep_sid);
		if (conn != NULL) {
			c2_net_conn_release(conn);
			c2_net_conn_unlink(conn);
		}
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

   Must be called within the domain mutex.

   @param ep  The newly created end point.
   @param dom The domain pointer.
   @param sa  Pointer to the struct sockaddr_in
   @param id  Service id (non-zero)
   @retval 0 on success
   @retval -errno on failure.
   @post (ergo(rc == 0, sunrpc_ep_invariant(*epp)));

*/
static int sunrpc_ep_create(struct c2_net_end_point **epp,
			    struct c2_net_domain *dom,
			    struct sockaddr_in *sa,
			    uint32_t id)
{
	int rc = 0;
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	struct c2_net_bulk_sunrpc_domain_pvt *dp = dom->nd_xprt_private;

	C2_PRE(sunrpc_dom_invariant(dom));
	/* C2_PRE(id > 0);*/
	/* create the base transport ep first */
	rc = (*dp->xd_base_ops.bmo_ep_create)(epp, dom, sa, id);
	if (rc != 0)
		return rc;

	mep = container_of(*epp, struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);
	sep->xep_magic = C2_NET_BULK_SUNRPC_XEP_MAGIC;

	/* create the sid (first time only) */
	if (!sep->xep_sid_valid) {
		char host[C2_NET_BULK_MEM_XEP_ADDR_LEN];
		char *p;
		int port = ntohs(mep->xep_sa.sin_port);
		/* create the service uuid */
		sprintf(sep->xep_sid.si_uuid,c2_net_bulk_sunrpc_uuid_fmt,port);
		C2_ASSERT(strlen(sep->xep_sid.si_uuid) <
			  sizeof(sep->xep_sid.si_uuid));
		/* copy the printable addr ("dotted_ip_addr:port") */
		strncpy(host, (*epp)->nep_addr, sizeof(host)-1);
		host[sizeof(host)-1] = '\0';
		for (p=host; *p && *p != ':'; p++);
		*p = '\0'; /* isolate the hostname */
		rc = c2_service_id_init(&sep->xep_sid, &dp->xd_rpc_dom,
					host, port);
		if (rc == 0) {
			sep->xep_sid_valid = true;
		} else {
			/* directly release the ep (we're in the dom mutex) */
			c2_ref_put(&(*epp)->nep_ref);
			*epp = NULL;
		}
	}

	C2_POST(ergo(rc == 0, sunrpc_ep_invariant(*epp)));
	return rc;
}

/**
   This subroutine returns a c2_net_conn pointer for the end point.
   It serializes against multiple concurrent invocations
   of itself using the domain mutex, so do not invoke while holding a
   transfer machine mutex.

   The caller is responsible for releasing the connection with the
   c2_net_conn_release() subroutine.

   @param ep End point pointer
   @param conn Returns the connection.
   @retval 0 Success
   @retval -errno Failure
   @post ergo(rc == 0, sep->xep_con_valid)
 */
static int sunrpc_ep_get_conn(struct c2_net_end_point *ep,
			      struct c2_net_conn **conn_p)
{
	int rc;
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);

	C2_PRE(sunrpc_ep_invariant(ep));

	rc = 0;
	if (!sep->xep_conn_created) { /* already exists? */
		/* create the connection in the mutex */
		c2_mutex_lock(&ep->nep_dom->nd_mutex);
		do {
			if (sep->xep_conn_created) /* racy, so check again */
				break;
			rc = c2_net_conn_create(&sep->xep_sid);
			if (rc != 0)
				break;
			sep->xep_conn_created = true;
		} while(0);
		c2_mutex_unlock(&ep->nep_dom->nd_mutex);
		rc = 0;
	}
	if (rc == 0) {
		*conn_p = c2_net_conn_find(&sep->xep_sid);
		if (*conn_p == NULL) {
			rc = -ECONNRESET;
			sep->xep_conn_created = false;
		}
	}

	C2_POST(ergo(rc == 0, sep->xep_conn_created));
	return rc;
}

/**
   Compare an end point with a sunrpc_ep for equality.
   @param ep End point
   @param sep sunrpc_ep pointer
   @param true Match
   @param false Do not match
 */
static bool sunrpc_ep_equals_addr(struct c2_net_end_point *ep,
				  struct sunrpc_ep *sep)
{
	C2_ASSERT(sunrpc_ep_invariant(ep));
	struct c2_net_bulk_mem_end_point *mep;
	mep = container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep);

	return (mep->xep_sa.sin_addr.s_addr == sep->sep_addr &&
		mep->xep_sa.sin_port        == sep->sep_port);
}

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
	    .sbd_id                  = buf_id,
	    .sbd_qtype               = qt,
	    .sbd_total               = buflen,
	    /* address and port numbers in network byte order */
	    .sbd_active_ep.sep_addr  = MEM_EP_ADDR(ep),
	    .sbd_active_ep.sep_port  = MEM_EP_PORT(ep),
	    .sbd_passive_ep.sep_addr = MEM_EP_ADDR(tm->ntm_ep),
	    .sbd_passive_ep.sep_port = MEM_EP_PORT(tm->ntm_ep),
	};

	desc->nbd_len = sizeof(sd);
	desc->nbd_data = c2_alloc(desc->nbd_len);
	if (desc->nbd_data == NULL)
	    return -ENOMEM;

	XDR xdrs;
	int rc = 0;
	xdrmem_create(&xdrs, desc->nbd_data, desc->nbd_len, XDR_ENCODE);
	if (!sunrpc_buf_desc_memlayout.fm_uxdr(&xdrs, &sd))
		rc = -EINVAL;
	xdr_destroy(&xdrs);
	return rc;
}

/**
   Decodes a network buffer descriptor.
   @param desc Network buffer descriptor pointer. Address and port
   numbers are in network byte order.
   @param sd Returns the descriptor contents.
   @retval 0 On success
   @retval -EINVAL Invalid transfer descriptor
 */
static int sunrpc_desc_decode(struct c2_net_buf_desc *desc,
			      struct sunrpc_buf_desc *sd)
{
	XDR xdrs;
	int rc = 0;
	xdrmem_create(&xdrs, desc->nbd_data, desc->nbd_len, XDR_DECODE);
	if (!sunrpc_buf_desc_memlayout.fm_uxdr(&xdrs, sd))
		rc = -EINVAL;
	xdr_destroy(&xdrs);
	return rc;
}

/**
   Compares if two descriptors are equal.
 */
static bool sunrpc_desc_equal(struct c2_net_buf_desc *d1,
			      struct sunrpc_buf_desc *sd2)
{
	/* could do a byte comparison too */
	struct sunrpc_buf_desc sd1;
	if (sunrpc_desc_decode(d1, &sd1))
		return false;
	if (sd1.sbd_id == sd2->sbd_id &&
	    sd1.sbd_active_ep.sep_addr == sd2->sbd_active_ep.sep_addr &&
	    sd1.sbd_active_ep.sep_port == sd2->sbd_active_ep.sep_port &&
	    sd1.sbd_passive_ep.sep_addr == sd2->sbd_passive_ep.sep_addr &&
	    sd1.sbd_passive_ep.sep_port == sd2->sbd_passive_ep.sep_port)
		return true;
	return false;
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
