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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */

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
   Subroutine to be invoked by the domain skulker thread to
   age cached end points.
   @param dom The domain pointer.  The domain mutex is held.
   @param now The epoch time value, or C2_TIME_NEVER to force the aging.
 */
static void sunrpc_skulker_process_end_points(struct c2_net_domain *dom,
					      c2_time_t now)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	struct c2_net_end_point *ep;
	struct c2_net_end_point *ep_next;
	struct c2_net_bulk_sunrpc_end_point *sep;
	c2_time_t free_if_before;
	c2_time_t t;

	C2_PRE(c2_mutex_is_locked(&dom->nd_mutex));

	dp = sunrpc_dom_to_pvt(dom);
	if (dp->xd_ep_release_delay == 0)
		now = C2_TIME_NEVER; /* switched off: force aging */
	if (now != C2_TIME_NEVER) {
		free_if_before = c2_time_sub(now, dp->xd_ep_release_delay);
	} else {
		free_if_before = C2_TIME_NEVER; /* force aging */
	}
	/* walk the ep list, potentially deleting entries as we go */
	c2_list_for_each_entry_safe(&dom->nd_end_points, ep, ep_next,
				    struct c2_net_end_point,
				    nep_dom_linkage) {
		sep = sunrpc_ep_to_pvt(ep);
		t = c2_atomic64_get(&sep->xep_last_use);
		if (c2_time_after(t, free_if_before))
			continue;
		c2_ref_get(&ep->nep_ref);
		c2_atomic64_set(&sep->xep_last_use, 0);
		/* Release the reference, potentially freeing the entry */
		c2_ref_put(&ep->nep_ref);
	}
	return;
}

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
	struct c2_net_bulk_sunrpc_end_point *sep;
	struct c2_net_bulk_sunrpc_domain_pvt *dp;

	ep = container_of(ref, struct c2_net_end_point, nep_ref);
	C2_PRE(sunrpc_ep_invariant(ep));
	C2_PRE(c2_mutex_is_locked(&ep->nep_dom->nd_mutex));
	sep = sunrpc_ep_to_pvt(ep);
	dp = sunrpc_dom_to_pvt(ep->nep_dom);

	/* If end point release delay is enabled, we delay the release if the
	   connection last_use time is set.
	   Note that this feature relies on logic in the underlying sunrpc
	   transport to silently reset a stale CLIENT structure on ECONNRESET.
	*/
	if (dp->xd_ep_release_delay != 0 &&
	    c2_atomic64_get(&sep->xep_last_use) > 0)
		return;

	/* free the conn and sid */
	if (sep->xep_conn_created) {
		struct c2_net_conn *conn = c2_net_conn_find(&sep->xep_sid);
		if (conn != NULL) {
			c2_net_conn_release(conn);
			c2_net_conn_unlink(conn);
		}
	}
	c2_service_id_fini(&sep->xep_sid);

	/* release the end point with the base method */
	(*dp->xd_base_ops->bmo_ep_release)(ref);
}

/**
   Internal subroutine to initialize a sid from an end point.
   @param sid Pointer to service id
   @param rpc_dom Domain of the underlying sunrpc transport.
   @param ep An end point pointer from any bulk sunrpc domain.
*/
static int sunrpc_ep_init_sid(struct c2_service_id *sid,
			      struct c2_net_domain *rpc_dom,
			      struct c2_net_end_point *ep)
{
	int rc = 0;
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	char host[C2_NET_BULK_MEM_XEP_ADDR_LEN];
	char *p;
	int port;

	sep = sunrpc_ep_to_pvt(ep);
	C2_PRE(sep->xep_magic == C2_NET_BULK_SUNRPC_XEP_MAGIC);
	/* get the port number from the base end point */
	mep = mem_ep_to_pvt(ep);
	port = ntohs(mep->xep_sa.sin_port);
	/* create the service uuid */
	sprintf(sid->si_uuid, c2_net_bulk_sunrpc_uuid_fmt, port);
	C2_ASSERT(strlen(sid->si_uuid) < sizeof(sid->si_uuid));
	/* copy the printable addr ("dotted_ip_addr:port:service_id") */
	strncpy(host, ep->nep_addr, sizeof(host) - 1);
	host[sizeof(host) - 1] = '\0';
	p = strchr(host, ':');
	if (p != NULL)
		*p = '\0'; /* isolate the hostname */
	rc = c2_service_id_init(sid, rpc_dom, host, port);
	return rc;
}

/**
   Allocate memory for a transport end point.
*/
static struct c2_net_bulk_mem_end_point *sunrpc_ep_alloc(void)
{
	struct c2_net_bulk_sunrpc_end_point *sep = c2_alloc(sizeof *sep);
	sep->xep_magic = C2_NET_BULK_SUNRPC_XEP_MAGIC;
	c2_atomic64_set(&sep->xep_last_use, 0);
	return &sep->xep_base; /* base pointer required */
}

/**
   Free memory for a transport end point.
*/
static void sunrpc_ep_free(struct c2_net_bulk_mem_end_point *mep)
{
	struct c2_net_bulk_sunrpc_end_point *sep =
		sunrpc_ep_to_pvt(&mep->xep_ep);
	C2_ASSERT(sep->xep_magic == C2_NET_BULK_SUNRPC_XEP_MAGIC);
	sep->xep_magic = 0;
	c2_free(sep);
}

static void sunrpc_ep_get(struct c2_net_end_point *ep)
{
	/* Directly obtain the ref count to avoid the non-zero ref count
	   assertion in c2_net_end_point_get().
	*/
	c2_ref_get(&ep->nep_ref);
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
			    const struct sockaddr_in *sa,
			    uint32_t id)
{
	int rc = 0;
	struct c2_net_bulk_sunrpc_end_point *sep;
	struct c2_net_bulk_sunrpc_domain_pvt *dp;

	C2_PRE(sunrpc_dom_invariant(dom));
	dp = sunrpc_dom_to_pvt(dom);
	/* C2_PRE(id > 0);*/
	/* create the base transport ep first - sunrpc_ep_alloc invoked */
	rc = (*dp->xd_base_ops->bmo_ep_create)(epp, dom, sa, id);
	if (rc != 0)
		return rc;

	sep = sunrpc_ep_to_pvt(*epp);
	C2_ASSERT(sep->xep_magic == C2_NET_BULK_SUNRPC_XEP_MAGIC);

	/* create the sid (first time only) */
	if (!sep->xep_sid_valid) {
		rc = sunrpc_ep_init_sid(&sep->xep_sid, &dp->xd_rpc_dom, *epp);
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
   sunrpc_ep_put_conn() subroutine.

   @param ep End point pointer
   @param conn Returns the connection.
   @retval 0 Success
   @retval -errno Failure
   @post ergo(rc == 0, sep->xep_con_created)
 */
static int sunrpc_ep_get_conn(struct c2_net_end_point *ep,
			      struct c2_net_conn **conn_p)
{
	int rc;
	struct c2_net_bulk_sunrpc_end_point *sep;
	sep = sunrpc_ep_to_pvt(ep);

	C2_PRE(sunrpc_ep_invariant(ep));

	rc = 0;
	if (!sep->xep_conn_created) { /* already exists? */
		/* create the connection in the mutex */
		c2_mutex_lock(&ep->nep_dom->nd_mutex);
		if (!sep->xep_conn_created) { /* racy, so check again */
			rc = c2_net_conn_create(&sep->xep_sid);
			if (rc == 0)
				sep->xep_conn_created = true;
		}
		c2_mutex_unlock(&ep->nep_dom->nd_mutex);
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
   Release the c2_net_conn structure returned by sunrpc_ep_get_conn().
   The subroutine records the time the connection was released.
   The end point will be cached for a while after last use, unless the
   error code is non-zero.
   @param ep End point pointer
   @param conn The connection.
   @param rc  The error code encountered during use.
 */
static void sunrpc_ep_put_conn(struct c2_net_end_point *ep,
			       struct c2_net_conn *conn,
			       int rc)
{
	struct c2_net_bulk_sunrpc_end_point *sep;
	c2_time_t t;
	sep = sunrpc_ep_to_pvt(ep);
	C2_PRE(sunrpc_ep_invariant(ep));

	c2_net_conn_release(conn);

	if (rc == 0)
		c2_time_now(&t);
	else
		t = 0;
	C2_CASSERT(sizeof t == sizeof sep->xep_last_use);
	c2_atomic64_set(&sep->xep_last_use, t);
	return;
}

/**
   Compare an end point with a sunrpc_ep for equality.
   @param ep End point
   @param sep sunrpc_ep pointer
   @param true Match
   @param false Do not match
 */
static bool sunrpc_ep_equals_addr(const struct c2_net_end_point *ep,
				  const struct sunrpc_ep *sep)
{
	struct c2_net_bulk_mem_end_point *mep;
	C2_ASSERT(sunrpc_ep_invariant(ep));
	mep = mem_ep_to_pvt(ep);

	return (mep->xep_sa.sin_addr.s_addr == sep->sep_addr &&
		mep->xep_sa.sin_port        == sep->sep_port &&
		mep->xep_service_id         == sep->sep_id);
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
#ifdef __KERNEL__
	C2_IMPOSSIBLE("Port in progress");
	return 0;
#else
	struct sunrpc_buf_desc sd = {
	    .sbd_id                  = buf_id,
	    .sbd_qtype               = qt,
	    .sbd_total               = buflen,
	    /* address and port numbers in network byte order */
	    .sbd_active_ep.sep_addr  = mem_ep_addr(ep),
	    .sbd_active_ep.sep_port  = mem_ep_port(ep),
	    .sbd_active_ep.sep_id    = mem_ep_sid(ep),
	    .sbd_passive_ep.sep_addr = mem_ep_addr(tm->ntm_ep),
	    .sbd_passive_ep.sep_port = mem_ep_port(tm->ntm_ep),
	    .sbd_passive_ep.sep_id   = mem_ep_sid(tm->ntm_ep),
	};
	XDR xdrs;
	int rc = 0;

	desc->nbd_len = sizeof(sd);
	desc->nbd_data = c2_alloc(desc->nbd_len);
	if (desc->nbd_data == NULL)
	    return -ENOMEM;

	xdrmem_create(&xdrs, desc->nbd_data, desc->nbd_len, XDR_ENCODE);
	if (!sunrpc_buf_desc_memlayout.fm_uxdr(&xdrs, &sd))
		rc = -EINVAL;
	xdr_destroy(&xdrs);
	return rc;
#endif
}

/**
   Decodes a network buffer descriptor.
   @param desc Network buffer descriptor pointer. Address and port
   numbers are in network byte order.
   @param sd Returns the descriptor contents.
   @retval 0 On success
   @retval -EINVAL Invalid transfer descriptor
 */
static int sunrpc_desc_decode(const struct c2_net_buf_desc *desc,
			      struct sunrpc_buf_desc *sd)
{
#ifdef __KERNEL__
	C2_IMPOSSIBLE("Port in progress");
	return 0;
#else
	XDR xdrs;
	int rc = 0;
	xdrmem_create(&xdrs, desc->nbd_data, desc->nbd_len, XDR_DECODE);
	if (!sunrpc_buf_desc_memlayout.fm_uxdr(&xdrs, sd))
		rc = -EINVAL;
	xdr_destroy(&xdrs);
	return rc;
#endif
}

/**
   Compares if two descriptors are equal.
 */
static bool sunrpc_desc_equal(const struct c2_net_buf_desc *d1,
			      const struct sunrpc_buf_desc *sd2)
{
	/* could do a byte comparison too */
	struct sunrpc_buf_desc sd1;
	if (sunrpc_desc_decode(d1, &sd1))
		return false;
	if (sd1.sbd_id == sd2->sbd_id &&
	    sd1.sbd_active_ep.sep_addr == sd2->sbd_active_ep.sep_addr &&
	    sd1.sbd_active_ep.sep_port == sd2->sbd_active_ep.sep_port &&
	    sd1.sbd_active_ep.sep_id == sd2->sbd_active_ep.sep_id &&
	    sd1.sbd_passive_ep.sep_addr == sd2->sbd_passive_ep.sep_addr &&
	    sd1.sbd_passive_ep.sep_port == sd2->sbd_passive_ep.sep_port &&
	    sd1.sbd_passive_ep.sep_id == sd2->sbd_passive_ep.sep_id)
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
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
