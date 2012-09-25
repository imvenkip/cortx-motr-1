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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#include <stdio.h>     /* fprintf */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <string.h>    /* strtok_r, strcmp */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/getopts.h"
#include "lib/misc.h"
#include "lib/finject.h"    /* C2_FI_ENABLED */

#include "balloc/balloc.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc2.h"
#include "reqh/reqh.h"
#include "colibri/cs_internal.h"
#include "colibri/magic.h"
#include "rpc/rpclib.h"

/**
   @addtogroup colibri_setup
   @{
 */

static const struct c2_addb_loc cs_addb_loc = {
        .al_name = "Colibri setup"
};

static const struct c2_addb_ctx_type cs_addb_ctx_type = {
        .act_name = "Colibri setup"
};

C2_ADDB_EV_DEFINE(arg_fail, "argument_failure",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(setup_fail, "setup_failure",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(storage_init_fail, "storage_init_failure",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(endpoint_init_fail, "endpoint_init_failure",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(service_init_fail, "service_init_failure",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(rpc_init_fail, "rpc_init_failure",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(reqh_init_fail, "reqh_int_failure",
                  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
   Represents cob domain id, it is incremented for every new cob domain.

   @todo Have a generic mechanism to generate unique cob domain id.
 */
static int cdom_id;

C2_TL_DESCR_DEFINE(cs_buffer_pools, "buffer pools in the colibri context",
		   static, struct cs_buffer_pool, cs_bp_linkage, cs_bp_magic,
		   C2_CS_BUFFER_POOL_MAGIC, C2_CS_BUFFER_POOL_HEAD_MAGIC);
C2_TL_DEFINE(cs_buffer_pools, static, struct cs_buffer_pool);

C2_TL_DESCR_DEFINE(cs_eps, "cs endpoints", static, struct cs_endpoint_and_xprt,
		   ex_linkage, ex_magix, C2_CS_ENDPOINT_AND_XPRT_MAGIC,
		   C2_CS_EPS_HEAD_MAGIC);

C2_TL_DEFINE(cs_eps, static, struct cs_endpoint_and_xprt);

static struct c2_bob_type cs_eps_bob;
C2_BOB_DEFINE(static, &cs_eps_bob, cs_endpoint_and_xprt);

/**
   Currently supported stob types in colibri context.
 */
static const char *cs_stypes[] = {
	[LINUX_STOB] = "Linux",
	[AD_STOB]    = "AD"
};

C2_TL_DESCR_DEFINE(rhctx, "reqh contexts", static, struct cs_reqh_context,
		   rc_linkage, rc_magix, C2_CS_REQH_CTX_MAGIC,
		   C2_CS_REQH_CTX_HEAD_MAGIC);

C2_TL_DEFINE(rhctx, static, struct cs_reqh_context);

static struct c2_bob_type rhctx_bob;
C2_BOB_DEFINE(static, &rhctx_bob, cs_reqh_context);

C2_TL_DESCR_DEFINE(ndom, "network domains", static, struct c2_net_domain,
		   nd_app_linkage, nd_magix, C2_NET_DOMAIN_MAGIC,
		   C2_CS_NET_DOMAIN_HEAD_MAGIC);

C2_TL_DEFINE(ndom, static, struct c2_net_domain);

static struct c2_bob_type ndom_bob;
C2_BOB_DEFINE(static, &ndom_bob, c2_net_domain);

C2_TL_DESCR_DEFINE(astob, "ad stob domains", static, struct cs_ad_stob,
		   as_linkage, as_magix, C2_CS_AD_STOB_MAGIC,
		   C2_CS_AD_STOB_HEAD_MAGIC);
C2_TL_DEFINE(astob, static, struct cs_ad_stob);

static struct c2_bob_type astob_bob;
C2_BOB_DEFINE(static, &astob_bob, cs_ad_stob);

static struct c2_net_domain *cs_net_domain_locate(struct c2_colibri *cctx,
						  const char *xprt);

static int reqh_ctx_args_are_valid(const struct cs_reqh_context *rctx)
{
	return rctx->rc_stype != NULL && rctx->rc_stpath != NULL &&
	       rctx->rc_dbpath != NULL && rctx->rc_snr != 0 &&
	       rctx->rc_services != NULL &&
	       !cs_eps_tlist_is_empty(&rctx->rc_eps);
}

/**
   Checks consistency of request handler context.
 */
static bool cs_reqh_context_invariant(const struct cs_reqh_context *rctx)
{
	return cs_reqh_context_bob_check(rctx) &&
	       C2_IN(rctx->rc_state, (RC_UNINITIALISED, RC_INITIALISED)) &&
	       rctx->rc_max_services == c2_reqh_service_types_length() &&
	       c2_tlist_invariant(&cs_eps_tl, &rctx->rc_eps) &&
	       reqh_ctx_args_are_valid(rctx) && rctx->rc_colibri != NULL &&
	       ergo(rctx->rc_state == RC_INITIALISED,
	       c2_reqh_invariant(&rctx->rc_reqh));
}

/**
   Looks up an xprt by the name.

   @param xprt_name Network transport name
   @param xprts Array of network transports supported in a colibri environment
   @param xprts_nr Size of xprts array

   @pre xprt_name != NULL && xprts != NULL && xprts_nr > 0

 */
static struct c2_net_xprt *cs_xprt_lookup(const char *xprt_name,
					  struct c2_net_xprt **xprts,
					  size_t xprts_nr)
{
        size_t i;

	C2_PRE(xprt_name != NULL && xprts != NULL && xprts_nr > 0);

        for (i = 0; i < xprts_nr; ++i)
                if (strcmp(xprt_name, xprts[i]->nx_name) == 0)
                        return xprts[i];
        return NULL;
}

/**
   Lists supported network transports.
 */
static void cs_xprts_list(FILE *out, struct c2_net_xprt **xprts, size_t xprts_nr)
{
        int i;

	C2_PRE(out != NULL && xprts != NULL);

        fprintf(out, "\nSupported transports:\n");
        for (i = 0; i < xprts_nr; ++i)
                fprintf(out, " %s\n", xprts[i]->nx_name);
}

/**
   Lists supported stob types.
 */
static void cs_stob_types_list(FILE *out)
{
	int i;

	C2_PRE(out != NULL);

	fprintf(out, "\nSupported stob types:\n");
	for (i = 0; i < STOBS_NR; ++i)
		fprintf(out, " %s\n", cs_stypes[i]);
}

/**
   Checks if the specified storage type is supported in a colibri context.

   @param stype Storage type

   @pre stype != NULL
 */
static bool stype_is_valid(const char *stype)
{
	C2_PRE(stype != NULL);

	return  strcasecmp(stype, cs_stypes[AD_STOB]) == 0 ||
		strcasecmp(stype, cs_stypes[LINUX_STOB]) == 0;
}

/**
   Checks if given network transport and network endpoint address are already
   in use in a request handler context.

   @param cctx Colibri context
   @param xprt Network transport
   @param ep Network end point address

   @pre cctx != NULL && xprt != NULL && ep != NULL
 */
static bool cs_endpoint_is_duplicate(struct c2_colibri *cctx,
				     const struct c2_net_xprt *xprt,
				     const char *ep)
{
	int                          cnt;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep_xprt;

	C2_PRE(cctx != NULL && xprt != NULL && ep != NULL);

	cnt = 0;
	c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_invariant(rctx));
		c2_tl_for(cs_eps, &rctx->rc_eps, ep_xprt) {
			C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep_xprt));
			if (strcmp(xprt->nx_name, "lnet") == 0) {
				if(c2_net_lnet_ep_addr_net_cmp(
					ep_xprt->ex_endpoint, ep) == 0 &&
				   strcmp(ep_xprt->ex_xprt, xprt->nx_name) == 0)
					++cnt;
			} else {
				if (strcmp(ep_xprt->ex_endpoint, ep) == 0 &&
				    strcmp(ep_xprt->ex_xprt, xprt->nx_name)
					   == 0)
					++cnt;
			}
			if (cnt > 1)
				return true;
		} c2_tl_endfor;
	} c2_tl_endfor;

	return false;
}

/**
   Checks if given network endpoint address and network transport are valid
   and if they are already in use in given colibri context.

   @param cctx Colibri context
   @param ep Network endpoint address
   @param xprt_name Network transport name

   @pre cctx != NULL && ep != NULL && xprt_name != NULL

   @retval 0 On success
	-EINVAL If endpoint is invalid
	-EADDRINUSE If endpoint is already in use
*/
static int cs_endpoint_validate(struct c2_colibri *cctx, const char *ep,
				const char *xprt_name)
{
	int                 rc;
	struct c2_net_xprt *xprt;

	C2_PRE(cctx != NULL && ep != NULL && xprt_name != NULL);

	rc = 0;
	xprt = cs_xprt_lookup(xprt_name, cctx->cc_xprts, cctx->cc_xprts_nr);
	if (xprt == NULL)
		rc = -EINVAL;

	if (rc == 0 && cs_endpoint_is_duplicate(cctx, xprt, ep))
		rc = -EADDRINUSE;

	return rc;
}

/**
   Extracts network transport name and network endpoint address from given
   colibri endpoint.
   Colibri endpoint is of 2 parts network xprt:network endpoint.
 */
static int ep_and_xprt_get(struct cs_reqh_context *rctx, const char *ep,
			   struct cs_endpoint_and_xprt **ep_xprt)
{
	int                          rc = 0;
	int                          ep_len = min32u(strlen(ep) + 1,
						     CS_MAX_EP_ADDR_LEN);
	char                        *sptr;
	struct cs_endpoint_and_xprt *epx;
	char                        *endpoint;
	struct c2_addb_ctx          *addb;

	C2_PRE(ep != NULL);

	addb = &rctx->rc_colibri->cc_addb;
	C2_ALLOC_PTR_ADDB(epx, addb, &cs_addb_loc);
	if (epx == NULL)
		return -ENOMEM;
	epx->ex_cep = ep;
	C2_ALLOC_ARR_ADDB(epx->ex_scrbuf, ep_len, addb, &cs_addb_loc);
	if (epx->ex_scrbuf == NULL) {
		c2_free(epx);
		return -ENOMEM;
	}
	strncpy(epx->ex_scrbuf, ep, ep_len);
	epx->ex_xprt = strtok_r(epx->ex_scrbuf, ":", &sptr);
	if (epx->ex_xprt == NULL)
		rc = -EINVAL;
	else {
		endpoint = strtok_r(NULL , "\0", &sptr);
		if (endpoint == NULL)
			rc = -EINVAL;
		else {
			epx->ex_endpoint = endpoint;
			cs_endpoint_and_xprt_bob_init(epx);
			cs_eps_tlink_init_at_tail(epx, &rctx->rc_eps);
			*ep_xprt = epx;
		}
	}

	if (rc != 0) {
		c2_free(epx->ex_scrbuf);
		c2_free(epx);
		*ep_xprt = NULL;
	}

	return rc;
}

/**
   Checks if specified service has already a duplicate entry in given request
   handler context.
 */
static bool service_is_duplicate(const struct cs_reqh_context *rctx,
				 const char *sname)
{
	int                     idx;
	int                     cnt;

	C2_PRE(rctx != NULL);

	C2_ASSERT(cs_reqh_context_invariant(rctx));
	for (idx = 0, cnt = 0; idx < rctx->rc_snr; ++idx) {
		if (strcasecmp(rctx->rc_services[idx], sname) == 0)
			++cnt;
		if (cnt > 1)
			return true;
	}

	return false;
}

/**
   Allocates a request handler and adds it to the list of the same in given
   colibri context.

   @param cctx Colibri context

   @see c2_colibri
 */
static struct cs_reqh_context *cs_reqh_ctx_alloc(struct c2_colibri *cctx)
{
	struct cs_reqh_context *rctx;

	C2_PRE(cctx != NULL);

	C2_ALLOC_PTR_ADDB(rctx, &cctx->cc_addb, &cs_addb_loc);
	if (rctx == NULL)
		goto out;

	rctx->rc_max_services = c2_reqh_service_types_length();
	if (rctx->rc_max_services == 0) {
		C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc, service_init_fail,
			    "No services available", -ENOENT);
		goto cleanup_rctx;
	}

	C2_ALLOC_ARR_ADDB(rctx->rc_services, rctx->rc_max_services,
					&cctx->cc_addb, &cs_addb_loc);
	if (rctx->rc_services == NULL)
		goto cleanup_rctx;

	cs_reqh_context_bob_init(rctx);
	cs_eps_tlist_init(&rctx->rc_eps);
	rhctx_tlink_init_at_tail(rctx, &cctx->cc_reqh_ctxs);
	rctx->rc_colibri = cctx;

	goto out;

cleanup_rctx:
	c2_free(rctx);
	rctx = NULL;
out:
	return rctx;
}

static void cs_reqh_ctx_free(struct cs_reqh_context *rctx)
{
	struct cs_endpoint_and_xprt *ep;

	C2_PRE(rctx != NULL);

	c2_tl_for(cs_eps, &rctx->rc_eps, ep) {
		C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		C2_ASSERT(ep->ex_scrbuf != NULL);
		c2_free(ep->ex_scrbuf);
		cs_eps_tlink_del_fini(ep);
		cs_endpoint_and_xprt_bob_fini(ep);
		c2_free(ep);
	} c2_tl_endfor;

	cs_eps_tlist_fini(&rctx->rc_eps);
	c2_free(rctx->rc_services);
	rhctx_tlink_del_fini(rctx);
	cs_reqh_context_bob_fini(rctx);
	c2_free(rctx);
}

/**
   Finds network domain for specified network transport in a given colibri
   context.

   @param cctx Colibri context
   @param xprt_name Type of network transport to be initialised

   @pre cctx != NULL && xprt_name != NULL

   @see c2_cs_init()
 */
static struct c2_net_domain *cs_net_domain_locate(struct c2_colibri *cctx,
						  const char *xprt_name)
{
	struct c2_net_domain *ndom;

	C2_PRE(cctx != NULL && xprt_name != NULL);

	c2_tl_for(ndom, &cctx->cc_ndoms, ndom) {
		C2_ASSERT(c2_net_domain_bob_check(ndom));
		if (strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0)
			break;
	} c2_tl_endfor;

	return ndom;
}

static struct c2_net_buffer_pool *cs_buffer_pool_get(struct c2_colibri *cctx,
						     struct c2_net_domain *ndom)
{
	struct cs_buffer_pool *cs_bp;

	C2_PRE(cctx != NULL);
	C2_PRE(ndom != NULL);

	c2_tl_for(cs_buffer_pools, &cctx->cc_buffer_pools, cs_bp) {
		if (cs_bp->cs_buffer_pool.nbp_ndom == ndom)
			return &cs_bp->cs_buffer_pool;
	} c2_tl_endfor;
	return NULL;
}

/**
   Initialises rpc machine for the given endpoint address.
   Once the new rpc_machine is created it is added to list of rpc machines
   in given request handler.
   Request handler should be initialised before invoking this function.

   @param cctx Colibri context
   @param xprt_name Network transport
   @param ep Network endpoint address
   @param tm_colour Unique colour to be assigned to each TM in a domain
   @param recv_queue_min_length Minimum number of buffers in TM receive queue
   @param max_rpc_msg_size Maximum RPC message size
   @param reqh Request handler to which the newly created
		rpc_machine belongs

   @pre cctx != NULL && xprt_name != NULL && ep != NULL && reqh != NULL
 */
static int cs_rpc_machine_init(struct c2_colibri *cctx, const char *xprt_name,
			       const char *ep, const uint32_t tm_colour,
			       const uint32_t recv_queue_min_length,
			       const uint32_t max_rpc_msg_size,
			       struct c2_reqh *reqh)
{
	struct c2_rpc_machine        *rpcmach;
	struct c2_net_domain         *ndom;
	struct c2_net_buffer_pool    *buffer_pool;
	int                           rc;

	C2_PRE(cctx != NULL && xprt_name != NULL && ep != NULL && reqh != NULL);

	ndom = cs_net_domain_locate(cctx, xprt_name);
	if (ndom == NULL)
		return -EINVAL;

	C2_ALLOC_PTR_ADDB(rpcmach, &cctx->cc_addb, &cs_addb_loc);
	if (rpcmach == NULL)
		return -ENOMEM;

	if (max_rpc_msg_size > c2_net_domain_get_max_buffer_size(ndom)) {
		c2_free(rpcmach);
		return -EINVAL;
	}

	buffer_pool = cs_buffer_pool_get(cctx, ndom);
	rc = c2_rpc_machine_init(rpcmach, reqh->rh_cob_domain, ndom, ep, reqh,
				 buffer_pool, tm_colour, max_rpc_msg_size,
				 recv_queue_min_length);
	if (rc != 0) {
		c2_free(rpcmach);
		return rc;
	}

	c2_reqh_rpc_mach_tlink_init_at_tail(rpcmach, &reqh->rh_rpc_machines);

	return rc;
}

/**
   Intialises rpc machines in a colibri context.

   @param cctx Colibri context
 */
static int cs_rpc_machines_init(struct c2_colibri *cctx)
{
	int                          rc = 0;
	FILE                        *ofd;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;

	C2_PRE(cctx != NULL);

	ofd = cctx->cc_outfile;
        c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_invariant(rctx));
		c2_tl_for(cs_eps, &rctx->rc_eps, ep) {
			C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_rpc_machine_init(cctx, ep->ex_xprt,
						 ep->ex_endpoint,
						 ep->ex_tm_colour,
						 rctx->rc_recv_queue_min_length,
						 rctx->rc_max_rpc_msg_size,
						 &rctx->rc_reqh);
			if (rc != 0) {
				C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    rpc_init_fail,
					    ep->ex_cep, rc);
				return rc;
			}
		} c2_tl_endfor;
	} c2_tl_endfor;

	return rc;
}

/**
   Finalises all the rpc machines from the list of rpc machines present in
   c2_reqh.

   @param reqh Request handler of which the rpc machines belong

   @pre reqh != NULL
 */
static void cs_rpc_machines_fini(struct c2_reqh *reqh)
{
	struct c2_rpc_machine *rpcmach;

	C2_PRE(reqh != NULL);

	c2_tl_for(c2_reqh_rpc_mach, &reqh->rh_rpc_machines, rpcmach) {
		C2_ASSERT(c2_rpc_machine_bob_check(rpcmach));
		c2_reqh_rpc_mach_tlink_del_fini(rpcmach);
		c2_rpc_machine_fini(rpcmach);
		c2_free(rpcmach);
	} c2_tl_endfor;
}

static uint32_t cs_domain_tms_nr(struct c2_colibri *cctx,
				struct c2_net_domain *dom)
{
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
        uint32_t		     cnt = 0;

	C2_PRE(cctx != NULL);

	c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		c2_tl_for(cs_eps, &rctx->rc_eps, ep) {
			if(strcmp(ep->ex_xprt, dom->nd_xprt->nx_name) == 0)
				ep->ex_tm_colour = cnt++;
		} c2_tl_endfor;
	} c2_tl_endfor;

	C2_POST(cnt > 0);

	return cnt;
}

/**
   It calculates the summation of the minimum receive queue length of all
   endpoints belong to a domain in all the reqest handler contexts.
 */
static uint32_t cs_dom_tm_min_recv_queue_total(struct c2_colibri *cctx,
					       struct c2_net_domain *dom)
{
	struct cs_reqh_context	    *rctx;
	struct cs_endpoint_and_xprt *ep;
	uint32_t		     min_queue_len_total = 0;

	C2_PRE(cctx != NULL);

	c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		c2_tl_for(cs_eps, &rctx->rc_eps, ep) {
			if(strcmp(ep->ex_xprt, dom->nd_xprt->nx_name) == 0)
				min_queue_len_total +=
					rctx->rc_recv_queue_min_length;
		} c2_tl_endfor;
	} c2_tl_endfor;
	return min_queue_len_total;
}

static void cs_buffer_pool_fini(struct c2_colibri *cctx)
{
	struct cs_buffer_pool   *cs_bp;

	C2_PRE(cctx != NULL);

	c2_tl_for(cs_buffer_pools, &cctx->cc_buffer_pools, cs_bp) {
                cs_buffer_pools_tlink_del_fini(cs_bp);
		c2_net_buffer_pool_fini(&cs_bp->cs_buffer_pool);
		c2_free(cs_bp);
	} c2_tl_endfor;
}

static int cs_buffer_pool_setup(struct c2_colibri *cctx)
{
	int		          rc = 0;
	struct c2_net_domain     *dom;
	struct cs_buffer_pool    *cs_bp;
	uint32_t		  tms_nr;
	uint32_t		  bufs_nr;
	uint32_t                  max_recv_queue_len;

	C2_PRE(cctx != NULL);

	c2_tl_for(ndom, &cctx->cc_ndoms, dom) {
		max_recv_queue_len = cs_dom_tm_min_recv_queue_total(cctx, dom);
		tms_nr		   = cs_domain_tms_nr(cctx, dom);
		C2_ASSERT(max_recv_queue_len >= tms_nr);

		bufs_nr  = c2_rpc_bufs_nr(max_recv_queue_len, tms_nr);

		C2_ALLOC_PTR(cs_bp);
		if (cs_bp == NULL) {
			rc = -ENOMEM;
			break;
		}

		rc = c2_rpc_net_buffer_pool_setup(dom, &cs_bp->cs_buffer_pool,
						  bufs_nr, tms_nr);
		if (rc != 0) {
			c2_free(cs_bp);
			break;
		}
		cs_buffer_pools_tlink_init_at_tail(cs_bp,
						   &cctx->cc_buffer_pools);
	} c2_tl_endfor;

	if (rc != 0)
		cs_buffer_pool_fini(cctx);

	return rc;
}

static int stob_file_id_get(yaml_document_t *doc, yaml_node_t *node,
			    uint64_t *id)
{
	const char       *key_str;
	yaml_node_pair_t *pair;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		key_str = (const char *)yaml_document_get_node(doc,
					pair->key)->data.scalar.value;
		if (strcasecmp(key_str, "id") == 0) {
			*id = atoll((const char *)yaml_document_get_node(doc,
				     pair->value)->data.scalar.value);
			return 0;
		}
	}

	return -ENOENT;
}

static const char *stob_file_path_get(yaml_document_t *doc, yaml_node_t *node)
{
	const char       *key_str;
	yaml_node_pair_t *pair;

	for (pair = node->data.mapping.pairs.start;
	     pair < node->data.mapping.pairs.top; ++pair) {
		key_str = (const char *)yaml_document_get_node(doc,
					pair->key)->data.scalar.value;
		if (strcasecmp(key_str, "filename") == 0)
			return (const char *)yaml_document_get_node(doc,
					     pair->value)->data.scalar.value;
	}

	return NULL;
}

static int cs_stob_file_load(const char *dfile, struct cs_stobs *stob,
			     struct c2_addb_ctx *addb)
{
        int               rc;
        FILE             *f;
	yaml_parser_t     parser;
	yaml_document_t  *document;

        f = fopen(dfile, "r");
        if (f == NULL)
                return -EINVAL;

	document = &stob->s_sfile.sf_document;
        rc = yaml_parser_initialize(&parser);
        if (rc != 1)
		return -EINVAL;

        yaml_parser_set_input_file(&parser, f);
        rc = yaml_parser_load(&parser, document);
        if (rc != 1)
                return -EINVAL;

	stob->s_sfile.sf_is_initialised = true;
	yaml_parser_delete(&parser);

	fclose(f);
        return 0;
}

static int cs_ad_stob_create(struct cs_stobs *stob, uint64_t cid,
			     struct c2_dbenv *db, const char *f_path,
			     struct c2_addb_ctx *addb)
{
	int                 rc;
	char                ad_dname[MAXPATHLEN];
	struct c2_stob_id  *bstob_id;
	struct c2_stob    **bstob;
	struct c2_balloc   *cb;
	struct cs_ad_stob  *adstob;
	struct c2_dtx      *tx;

	C2_ALLOC_PTR_ADDB(adstob, addb, &cs_addb_loc);
	if (adstob == NULL)
		return -ENOMEM;

        tx = &stob->s_tx;
	bstob = &adstob->as_stob_back;
	bstob_id = &adstob->as_id_back;
	bstob_id->si_bits.u_hi = cid;
	bstob_id->si_bits.u_lo = 0xadf11e;
	rc = c2_stob_find(stob->s_ldom, bstob_id, bstob);
	if (rc == 0) {
		if (f_path != NULL)
			rc = c2_linux_stob_link(stob->s_ldom, *bstob, f_path,
						tx);
		if (rc == 0 || rc == -EEXIST)
			rc = c2_stob_create_helper(stob->s_ldom, tx, bstob_id,
						   bstob);
	}

	if (rc == 0 && C2_FI_ENABLED("ad_domain_locate_fail"))
		rc = -EINVAL;

	if (rc == 0) {
		sprintf(ad_dname, "%lx%lx", bstob_id->si_bits.u_hi,
			bstob_id->si_bits.u_lo);
		rc = c2_stob_domain_locate(&c2_ad_stob_type, ad_dname,
					   &adstob->as_dom);
	}

	if (rc != 0)
		c2_free(adstob);

	if (rc == 0) {
		cs_ad_stob_bob_init(adstob);
		astob_tlink_init_at_tail(adstob, &stob->s_adoms);
	}

	if (rc == 0)
		rc = c2_balloc_allocate(&cb);
	if (rc == 0)
		rc = c2_ad_stob_setup(adstob->as_dom, db,
				      *bstob, &cb->cb_ballroom,
				      BALLOC_DEF_CONTAINER_SIZE,
				      BALLOC_DEF_BLOCK_SHIFT,
				      BALLOC_DEF_BLOCKS_PER_GROUP,
				      BALLOC_DEF_RESERVED_GROUPS);

	if (rc == 0 && C2_FI_ENABLED("ad_stob_setup_fail"))
		rc = -EINVAL;

	return rc;
}

/**
   Initialises AD type stob.
 */
static int cs_ad_stob_init(struct cs_stobs *stob, struct c2_dbenv *db,
			   struct c2_addb_ctx *addb)
{
        int                rc;
        int                result;
	uint64_t           cid;
	const char        *f_path;
	yaml_document_t   *doc;
	yaml_node_t       *node;
	yaml_node_t       *s_node;
	yaml_node_item_t  *item;

        C2_PRE(stob != NULL);

	astob_tlist_init(&stob->s_adoms);
	if (stob->s_sfile.sf_is_initialised) {
		doc = &stob->s_sfile.sf_document;
		for (node = doc->nodes.start; node < doc->nodes.top; ++node) {
			for (item = (node)->data.sequence.items.start;
			     item < (node)->data.sequence.items.top; ++item) {
				s_node = yaml_document_get_node(doc, *item);
				result = stob_file_id_get(doc, s_node, &cid);
				if (result != 0)
					continue;
				f_path = stob_file_path_get(doc, s_node);
				rc = cs_ad_stob_create(stob, cid, db,
						       f_path, addb);
				if (rc != 0)
					break;
			}
		}
	} else
		rc = cs_ad_stob_create(stob, AD_BACK_STOB_ID_DEFAULT, db, NULL,
				       addb);

	return rc;
}

/**
   Initialises linux type stob.
 */
static int cs_linux_stob_init(const char *stob_path, struct cs_stobs *stob)
{
	int                    rc;
	struct c2_stob_domain *sdom;

	rc = c2_stob_domain_locate(&c2_linux_stob_type, stob_path,
				   &stob->s_ldom);
	if (rc == 0) {
		sdom = stob->s_ldom;
		rc = c2_linux_stob_setup(sdom, false);
	}

	return rc;
}

static void cs_ad_stob_fini(struct cs_stobs *stob)
{
	struct c2_stob        *bstob;
	struct cs_ad_stob     *adstob;
	struct c2_stob_domain *adom;

	C2_PRE(stob != NULL);

	c2_tl_for(astob, &stob->s_adoms, adstob) {
		C2_ASSERT(cs_ad_stob_bob_check(adstob) &&
			  adstob->as_dom != NULL);
		bstob = adstob->as_stob_back;
		adom = adstob->as_dom;
		if (bstob != NULL && bstob->so_state == CSS_EXISTS)
			c2_stob_put(bstob);
		adom->sd_ops->sdo_fini(adom);
		astob_tlink_del_fini(adstob);
		cs_ad_stob_bob_fini(adstob);
		c2_free(adstob);
	} c2_tl_endfor;
	astob_tlist_fini(&stob->s_adoms);
}

void cs_linux_stob_fini(struct cs_stobs *stob)
{
	C2_PRE(stob != NULL);

	if (stob->s_ldom != NULL)
                stob->s_ldom->sd_ops->sdo_fini(stob->s_ldom);
}

struct c2_stob_domain *c2_cs_stob_domain_find(struct c2_reqh *reqh,
					      const struct c2_stob_id *stob_id)
{
	struct cs_reqh_context  *rqctx;
	struct cs_stobs         *stob;
	struct cs_ad_stob       *adstob;

	rqctx = bob_of(reqh, struct cs_reqh_context, rc_reqh, &rhctx_bob);
	stob = &rqctx->rc_stob;

	if (strcasecmp(stob->s_stype, cs_stypes[LINUX_STOB]) == 0)
		return stob->s_ldom;
	else if (strcasecmp(stob->s_stype, cs_stypes[AD_STOB]) == 0) {
		c2_tl_for(astob, &stob->s_adoms, adstob) {
			C2_ASSERT(cs_ad_stob_bob_check(adstob));
			if (!stob->s_sfile.sf_is_initialised ||
			    adstob->as_id_back.si_bits.u_hi ==
			    stob_id->si_bits.u_hi)
				return adstob->as_dom;
		} c2_tl_endfor;
	}

	return NULL;
}

/**
   Initialises storage including database environment and stob domain of given
   type (e.g. linux or ad). There is a stob domain and a database environment
   created per request handler context.

   @todo Use generic mechanism to generate stob ids
 */
static int cs_storage_init(const char *stob_type, const char *stob_path,
			   struct cs_stobs *stob, struct c2_dbenv *db,
			   struct c2_addb_ctx *addb)
{
	int               rc;
	int               slen;
	struct c2_dtx    *tx;
	char             *objpath;
	static const char objdir[] = "/o";

	C2_PRE(stob_type != NULL && stob_path != NULL && stob != NULL);

	stob->s_stype = stob_type;

	slen = strlen(stob_path);
	C2_ALLOC_ARR_ADDB(objpath, slen + ARRAY_SIZE(objdir), addb,
			  &cs_addb_loc);
	if (objpath == NULL)
		return -ENOMEM;

	sprintf(objpath, "%s%s", stob_path, objdir);

	rc = mkdir(stob_path, 0700);
        if (rc != 0 && errno != EEXIST)
		goto out;

        rc = mkdir(objpath, 0700);
        if (rc != 0 && errno != EEXIST)
		goto out;

	tx = &stob->s_tx;
	c2_dtx_init(tx);
	rc = c2_dtx_open(tx, db);
	if (rc != 0) {
		c2_dtx_done(tx);
		goto out;
	}
	rc = cs_linux_stob_init(stob_path, stob);
	if (rc != 0)
		goto out;

	if (strcasecmp(stob_type, cs_stypes[AD_STOB]) == 0)
		rc = cs_ad_stob_init(stob, db, addb);

out:
	c2_free(objpath);

	return rc;
}

/**
   Finalises storage for a request handler in a colibri context.
 */
static void cs_storage_fini(struct cs_stobs *stob)
{
	C2_PRE(stob != NULL);

	c2_dtx_done(&stob->s_tx);
	if (strcasecmp(stob->s_stype, cs_stypes[AD_STOB]) == 0)
		cs_ad_stob_fini(stob);
        cs_linux_stob_fini(stob);
	if (stob->s_sfile.sf_is_initialised)
		yaml_document_delete(&stob->s_sfile.sf_document);
}

/**
   Initialises and starts a particular service.
   Once the service is initialised it is started and registered with the
   appropriate request handler.

   @param service_name Name of service to be initialised
   @param reqh Request handler this service is to be registered with

   @pre service_name != NULL && reqh != NULL

   @post c2_reqh_service_invariant(service)
 */
static int cs_service_init(const char *service_name, struct c2_reqh *reqh)
{
	int                          rc;
	struct c2_reqh_service_type *stype;
	struct c2_reqh_service      *service;

	C2_PRE(service_name != NULL && reqh != NULL);

        stype = c2_reqh_service_type_find(service_name);
        if (stype == NULL)
                return -EINVAL;

	rc = c2_reqh_service_allocate(stype, &service);
	if (rc == 0) {
		c2_reqh_service_init(service, reqh);
		rc = c2_reqh_service_start(service);
		if (rc != 0)
			c2_reqh_service_fini(service);
	}

	return rc;
}

/**
   Initialises set of services specified in a request handler context.
   Services are started once the colibri context is configured successfully
   which includes network domains, request handlers, and rpc machines.

   @param cctx Colibri context
 */
static int cs_services_init(struct c2_colibri *cctx)
{
	int                     idx;
	int                     rc;
	struct cs_reqh_context *rctx;

	C2_PRE(cctx != NULL);

        c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_invariant(rctx));
		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			rc = cs_service_init(rctx->rc_services[idx],
					     &rctx->rc_reqh);
			if (rc != 0)
				return rc;
		}
	} c2_tl_endfor;

	return rc;
}

/**
   Finalises a service.
   Transitions service to C2_RSPH_STOPPING phase, stops a service and then
   finalises the same.

   @param service Service to be finalised

   @pre service != NULL

   @see c2_reqh_service_stop()
   @see c2_reqh_service_fini()
 */
static void cs_service_fini(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL);

	c2_reqh_service_stop(service);
	c2_reqh_service_fini(service);
}

/**
   Finalises all the services registered with a request handler.
   Also traverses through the services list and invokes cs_service_fini() on
   each individual service.

   @param reqh Request handler of which the services are to be finalised

   @pre reqh != NULL
 */
static void cs_services_fini(struct c2_reqh *reqh)
{
	struct c2_reqh_service *service;
	struct c2_tl           *services;

	C2_PRE(reqh != NULL);

	services = &reqh->rh_services;
        c2_tl_for(c2_reqh_svc, services, service) {
		C2_ASSERT(c2_reqh_service_invariant(service));
		cs_service_fini(service);
	} c2_tl_endfor;
}

/**
   Initialises network domains per given distinct xport:endpoint pair in a
   colibri context.

   @param cctx Colibri context
 */
static int cs_net_domains_init(struct c2_colibri *cctx)
{
	int                          rc;
	size_t                       xprts_nr;
	FILE                        *ofd;
	struct c2_net_xprt         **xprts;
	struct c2_net_xprt          *xprt;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	struct c2_net_domain        *ndom;

	C2_PRE(cctx != NULL);

	xprts = cctx->cc_xprts;
	xprts_nr = cctx->cc_xprts_nr;

	ofd = cctx->cc_outfile;
        c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_invariant(rctx));
		c2_tl_for(cs_eps, &rctx->rc_eps, ep) {
			C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));

			xprt = cs_xprt_lookup(ep->ex_xprt, xprts, xprts_nr);
			if (xprt == NULL) {
				C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    endpoint_init_fail,
					    ep->ex_xprt, -EINVAL);
				return -EINVAL;
			}

			ndom = cs_net_domain_locate(cctx, ep->ex_xprt);
			if (ndom != NULL)
				continue;

			rc = c2_net_xprt_init(xprt);
			if (rc != 0)
				return rc;

			C2_ALLOC_PTR_ADDB(ndom, &cctx->cc_addb, &cs_addb_loc);
			if (ndom == NULL) {
				c2_net_xprt_fini(xprt);
				return -ENOMEM;
			}
			rc = c2_net_domain_init(ndom, xprt);
			if (rc != 0) {
				c2_free(ndom);
				c2_net_xprt_fini(xprt);
				return rc;
			}
			c2_net_domain_bob_init(ndom);
			ndom_tlink_init_at_tail(ndom, &cctx->cc_ndoms);
		} c2_tl_endfor;
	} c2_tl_endfor;

	return rc;
}

/**
   Finalises all the network domains within a colibri context.

   @param cctx Colibri context to which the network domains belong
 */
static void cs_net_domains_fini(struct c2_colibri *cctx)
{
	struct c2_net_domain  *ndom;
	struct c2_net_xprt   **xprts;
	size_t                 idx;

	C2_PRE(cctx != NULL);

	xprts = cctx->cc_xprts;
	c2_tl_for(ndom, &cctx->cc_ndoms, ndom) {
		C2_ASSERT(c2_net_domain_bob_check(ndom));
		c2_net_domain_fini(ndom);
		ndom_tlink_del_fini(ndom);
		c2_net_domain_bob_fini(ndom);
		c2_free(ndom);
	} c2_tl_endfor;

	for (idx = 0; idx < cctx->cc_xprts_nr; ++idx)
		c2_net_xprt_fini(xprts[idx]);
}

/**
   Initialises a request handler context.
   A request handler context consists of the storage domain, database,
   cob domain, fol and request handler instance to be initialised.
   The request handler context is allocated and initialised per request handler
   in a colibri process per node. So, there can exist multiple request handlers
   and thus multiple request handler contexts in a colibri context.

   @param rctx Request handler context to be initialised
 */
static int cs_request_handler_start(struct cs_reqh_context *rctx)
{
	int                      rc;
	struct c2_addb_ctx      *addb;

	addb = &rctx->rc_colibri->cc_addb;
	rc = c2_dbenv_init(&rctx->rc_db, rctx->rc_dbpath, 0);
	if (rc != 0) {
		C2_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "c2_dbenv_init", rc);
		goto out;
	}
	if (rctx->rc_dfilepath != NULL) {
		rc = cs_stob_file_load(rctx->rc_dfilepath, &rctx->rc_stob,
				       addb);
		if (rc != 0) {
			C2_ADDB_ADD(addb, &cs_addb_loc,
				    storage_init_fail,
				    "Failed to load device configuration file",
				    rc);
			goto out;
		}
	}

	if (rc == 0)
		rc = cs_storage_init(rctx->rc_stype, rctx->rc_stpath,
				     &rctx->rc_stob, &rctx->rc_db, addb);
	if (rc != 0) {
		C2_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "cs_storage_init", rc);
		goto cleanup_stob;
	}
	rctx->rc_cdom_id.id = ++cdom_id;
	rc = c2_cob_domain_init(&rctx->rc_cdom, &rctx->rc_db,
				&rctx->rc_cdom_id);
	if (rc != 0) {
		C2_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "c2_cob_domain_init", rc);
		goto cleanup_stob;
	}
	rc = c2_fol_init(&rctx->rc_fol, &rctx->rc_db);
	if (rc != 0) {
		C2_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "c2_fol_init", rc);
		goto cleanup_cob;
	}
	rc = c2_reqh_init(&rctx->rc_reqh, NULL, &rctx->rc_db, &rctx->rc_cdom,
			  &rctx->rc_fol);
	if (rc != 0)
		goto cleanup_fol;

	rctx->rc_state = RC_INITIALISED;
	goto out;

cleanup_fol:
	c2_fol_fini(&rctx->rc_fol);
cleanup_cob:
	c2_cob_domain_fini(&rctx->rc_cdom);
cleanup_stob:
	cs_storage_fini(&rctx->rc_stob);
	c2_dbenv_fini(&rctx->rc_db);
out:
	return rc;
}

/**
   Configures one or more request handler contexts and starts corresponding
   request handlers in each context.
 */
static int cs_request_handlers_start(struct c2_colibri *cctx)
{
	int                     rc = 0;
	struct cs_reqh_context *rctx;
	FILE                   *ofd;

	C2_PRE(cctx != NULL);

	ofd = cctx->cc_outfile;
        c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_invariant(rctx));
		rc = cs_request_handler_start(rctx);
		if (rc != 0)
			return rc;
	} c2_tl_endfor;

	return rc;
}

/**
   Finalises a request handler context.
   Sets c2_reqh::rh_shutdown true, and checks if the request handler can be
   shutdown by invoking c2_reqh_can_shutdown().
   This waits until c2_reqh_can_shutdown() returns true and then proceeds for
   further cleanup.

   @param rctx Request handler context to be finalised

   @pre cs_reqh_context_invariant()
 */
static void cs_request_handler_stop(struct cs_reqh_context *rctx)
{
	struct c2_reqh *reqh;

	C2_PRE(cs_reqh_context_invariant(rctx));

	reqh = &rctx->rc_reqh;
	c2_reqh_shutdown_wait(reqh);

	cs_services_fini(reqh);
	cs_rpc_machines_fini(reqh);
	c2_reqh_fini(reqh);
	c2_fol_fini(&rctx->rc_fol);
	c2_cob_domain_fini(&rctx->rc_cdom);
	cs_storage_fini(&rctx->rc_stob);
	c2_dbenv_fini(&rctx->rc_db);
}

/**
   Finalises all the request handler contexts within a colibri context.

   @param cctx Colibri context to which the reqeust handler contexts belong
 */
static void cs_request_handlers_stop(struct c2_colibri *cctx)
{
	struct cs_reqh_context *rctx;

	C2_PRE(cctx != NULL);

	c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		if (rctx->rc_state == RC_INITIALISED)
			cs_request_handler_stop(rctx);
		cs_reqh_ctx_free(rctx);
	} c2_tl_endfor;
}

/**
   Find a request handler service within a given Colibri instance.

   @param cctx Pointer to Colibri context
   @param service_name Name of the service

   @pre cctx != NULL && service_name != NULL

   @retval  NULL of reqh instnace.
 */
struct c2_reqh *c2_cs_reqh_get(struct c2_colibri *cctx,
			       const char *service_name)
{
	int                      idx;
	struct cs_reqh_context  *rctx;
	struct c2_reqh		*reqh;

	C2_PRE(cctx != NULL);
	C2_PRE(service_name != NULL);

	c2_rwlock_read_lock(&cctx->cc_rwlock);
        c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_invariant(rctx));

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			if (strcmp(rctx->rc_services[idx],
				   service_name) == 0) {
				reqh = &rctx->rc_reqh;
				c2_rwlock_read_unlock(&cctx->cc_rwlock);
				return reqh;
			}
		}
	} c2_tl_endfor;
	c2_rwlock_read_unlock(&cctx->cc_rwlock);

	return NULL;

}
C2_EXPORTED(c2_cs_reqh_get);

static struct cs_reqh_context *cs_reqh_ctx_get(struct c2_reqh *reqh)
{
	struct cs_reqh_context *rqctx;

	C2_PRE(c2_reqh_invariant(reqh));

	rqctx = bob_of(reqh, struct cs_reqh_context, rc_reqh, &rhctx_bob);
	C2_POST(cs_reqh_context_invariant(rqctx));

	return rqctx;
}

struct c2_colibri *c2_cs_ctx_get(struct c2_reqh *reqh)
{
	return cs_reqh_ctx_get(reqh)->rc_colibri;
}

/**
   Initialises a colibri context.

   @param cctx Colibri context to be initialised

   @pre cctx != NULL
 */
static void cs_colibri_init(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);

	rhctx_tlist_init(&cctx->cc_reqh_ctxs);
	c2_bob_type_tlist_init(&rhctx_bob, &rhctx_tl);

	ndom_tlist_init(&cctx->cc_ndoms);
	c2_bob_type_tlist_init(&ndom_bob, &ndom_tl);
        cs_buffer_pools_tlist_init(&cctx->cc_buffer_pools);

	c2_bob_type_tlist_init(&cs_eps_bob, &cs_eps_tl);

	c2_bob_type_tlist_init(&astob_bob, &astob_tl);
	c2_rwlock_init(&cctx->cc_rwlock);

	c2_addb_ctx_init(&cctx->cc_addb, &cs_addb_ctx_type,
			 &c2_addb_global_ctx);
}

/**
   Finalises a colibri context.

   @pre cctx != NULL

   @param cctx Colibri context to be finalised
 */
static void cs_colibri_fini(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);

	rhctx_tlist_fini(&cctx->cc_reqh_ctxs);
	cs_buffer_pools_tlist_fini(&cctx->cc_buffer_pools);
	ndom_tlist_fini(&cctx->cc_ndoms);
	c2_rwlock_fini(&cctx->cc_rwlock);
	c2_addb_ctx_fini(&cctx->cc_addb);
}

/**
   Displays usage of colibri_setup program.

   @param out File to which the output is written
 */
static void cs_usage(FILE *out)
{
	C2_PRE(out != NULL);

	fprintf(out, "Usage: colibri_setup [-h] [-x] [-l]\n"
		   "    or colibri_setup GlobalFlags ReqHSpec+\n"
		   "       where\n"
		   "         GlobalFlags := [-M RPCMaxMessageSize]"
		   " [-Q MinReceiveQueueLength]\n"
		   "         ReqHspec    := -r -T StobType -DDBPath"
		   " -SStobFile [-dDevfile] {-e xport:endpoint}+\n"
		   "                        {-s service}+"
		   " [-q MinReceiveQueueLength] [-m RPCMaxMessageSize]\n");
}

/**
   Displays help for colibri_setup program.

   @param out File to which the output is written
 */
static void cs_help(FILE *out)
{
	C2_PRE(out != NULL);

	cs_usage(out);
	fprintf(out, "Every -r option represents a request handler set.\n"
		   "All the parameters in a request handler set are "
		   "mandatory.\nThere can be "
		   "multiple such request handler sets in a single colibri "
		   "process.\n"
		   "Endpoints and services can be specified multiple times "
		   "using -e and -s options\nin a request handler set.\n"
		   "-h Prints colibri usage help.\n"
		   "   e.g. colibri_setup -h\n"
		   "-x Lists supported network transports.\n"
		   "   e.g. colibri_setup -x\n"
		   "-l Lists supported services on this node.\n"
		   "   e.g. colibri_setup -l\n"
		   "-Q Minimum TM Receive queue length.\n"
		   "   It is a global and optional flag.\n"
		   "-M Maximum RPC message size.\n"
		   "   It is a global and optional flag.\n"
		   "-r Represents a request handler context.\n"
		   "-T Type of storage to be used by the request handler in "
		   "current context.\n"
		   "   This is specified once per request handler context, "
		   "e.g. linux, ad\n"
		   "-D Database file to be used in a request handler.\n"
		   "   This is specified once per request handler set.\n"
		   "-S Stob file for request handler context.\n"
		   "   This is specified once per request handler set.\n"
		   "-d Device configuration file path.\n"
		   "   This is an optional parameter specified once per "
		   "request handler.\n"
		   "   The device configuration file should contain device "
		   "id and\n   corresponding device path.\n"
		   "   e.g. id: 0\n"
		   "        filename: /dev/sda\n"
		   "   Note: Only AD type stob domain can be configured "
		   "over a device.\n"
		   "-e Network layer endpoint to which clients connect. "
		   "Network layer endpoint\n   consists of 2 parts "
		   "network transport:endpoint address.\n"
/* Currently cs_main.c does not pick up the in-mem transport. There is no
 * external use case for memxprt.
 * This does not prevent its usage in UT. So UT uses memxprt but the help
 * should not be given unless there is an external use case.
 */
#if 0
		   "   Currently supported transports are lnet and memxprt.\n "
		   "   lnet takes 4-tuple endpoint address\n"
		   "       NID : PID : PortalNumber : TransferMachineIdentifier\n"
		   "       e.g. lnet:172.18.50.40@o2ib1:12345:34:1\n"
		   "    whereas memxprt endpoint address can be given in two types,\n"
		   "       dottedIP:portNumber\n"
		   "       dottedIP:portNumber:serviceId\n"
		   "       e.g. memxprt:192.168.172.130:12345\n"
		   "            memxprt:255.255.255.255:65535:4294967295\n"
		   "   This can be specified multiple times, per request "
		   "handler set. Thus there\n   can exist multiple endpoints "
		   "per network transport with different transfer machine ids,\n"
		   "   i.e. 4th component of 4-tuple endpoint address in "
		   "lnet or 3rd component of \n   3-tuple endpoint address in memxprt.\n"
#else
		   "   Currently supported transport is lnet.\n "
		   "   lnet takes 4-tuple endpoint address\n"
		   "       NID : PID : PortalNumber : TransferMachineIdentifier\n"
		   "       e.g. lnet:172.18.50.40@o2ib1:12345:34:1\n"
		   "   This can be specified multiple times, per request "
		   "handler set. Thus there\n   can exist multiple endpoints "
		   "per network transport with different transfer machine ids,\n"
		   "   i.e. 4th component of 4-tuple endpoint address in lnet\n"
#endif /* 0 */
		   "-s Services to be started in given request handler "
		   "context.\n   This can be specified multiple times "
		   "per request handler set.\n"
		   "Note: Service type must be registered for a service to \n"
		   "be started.\n"
		   "Duplicate services are not allowed in the same request \n"
		   "handler context.\n"
		   "-q Minimum TM Receive queue length.\n"
		   "   If not set overrided by global value.\n"
		   "-m Maximum RPC message size.\n"
		   "   If not set overrided by global value.\n"
		   "   Should not be greater than XprtMaxBufferSize\n"
		   "\n"
		   "   e.g. ./colibri_setup -Q 4 -M 4096 -r -T linux\n"
		   "        -D dbpath -S stobfile -e lnet:172.18.50.40@o2ib1:12345:34:1 \n"
		   "	    -s mds -q 8 -m 65536 \n");
}

static int reqh_ctxs_are_valid(struct c2_colibri *cctx)
{
	int                          rc = 0;
	int                          idx;
	FILE                        *ofd;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	const char                  *sname;

	C2_PRE(cctx != NULL);

	if (cctx->cc_recv_queue_min_length < C2_NET_TM_RECV_QUEUE_DEF_LEN)
		cctx->cc_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN;

	ofd = cctx->cc_outfile;
        c2_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
                if (!reqh_ctx_args_are_valid(rctx)){
			C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
				    arg_fail,
				    "Missing or Invalid parameters",
				    -EINVAL);
                        return -EINVAL;
		}

		if (rctx->rc_recv_queue_min_length <
		    C2_NET_TM_RECV_QUEUE_DEF_LEN)
			rctx->rc_recv_queue_min_length =
				cctx->cc_recv_queue_min_length;

		if (rctx->rc_max_rpc_msg_size == 0)
			rctx->rc_max_rpc_msg_size =
				cctx->cc_max_rpc_msg_size;

		if (!stype_is_valid(rctx->rc_stype)) {
			C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
				    storage_init_fail,
				    rctx->rc_stype, -EINVAL);
			cs_stob_types_list(ofd);
                        return -EINVAL;
                }
		/*
		   Check if all the given end points in a reqh context are
		   valid.
		 */
		if (cs_eps_tlist_is_empty(&rctx->rc_eps)) {
			C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
				    arg_fail,
				    "Endpoint missing", -EINVAL);
			return -EINVAL;
		}

		c2_tl_for(cs_eps, &rctx->rc_eps, ep) {
			C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_endpoint_validate(cctx, ep->ex_endpoint,
						  ep->ex_xprt);
			if (rc != 0) {
				C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    endpoint_init_fail,
					    ep->ex_cep, rc);
				return rc;
			}
		} c2_tl_endfor;

		/*
		   Check if the services are registered and are valid in a
		   reqh context.
		 */
		if (rctx->rc_snr == 0) {
			C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
				    service_init_fail,
				    "No Service specified", -EINVAL);
			return -EINVAL;
		}

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			sname = rctx->rc_services[idx];
			if (!c2_reqh_service_is_registered(sname)) {
				C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    service_init_fail,
					    sname, -ENOENT);
				return -ENOENT;
			}
			if (service_is_duplicate(rctx, sname)) {
				C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    service_init_fail,
					    sname, -EEXIST);
				return -EEXIST;
			}
		}
	} c2_tl_endfor;

	return rc;
}

/**
   Parses given arguments and allocates request handler context, if all the
   required arguments are provided and valid.
   Every allocated request handler context is added to the list of the same
   in given colibri context.
 */
static int cs_parse_args(struct c2_colibri *cctx, int argc, char **argv)
{
	int                     rc = 0;
	int                     result;
	struct cs_reqh_context *rctx = NULL;
	FILE                   *ofd;

	C2_PRE(cctx != NULL);

	if (argc <= 1)
		return -EINVAL;;

	ofd = cctx->cc_outfile;
	result = C2_GETOPTS("colibri_setup", argc, argv,
			C2_VOIDARG('h', "Colibri_setup usage help",
				LAMBDA(void, (void)
				{
					cs_help(ofd);
					rc = 1;
				})),
			C2_VOIDARG('x', "List Supported transports",
				LAMBDA(void, (void)
				{
					cs_xprts_list(ofd, cctx->cc_xprts,
						      cctx->cc_xprts_nr);
					rc = 1;
				})),
			C2_VOIDARG('l', "List Supported services",
				LAMBDA(void, (void)
				{
					printf("Supported services:\n");
					c2_reqh_service_list_print();
					rc = 1;
				})),
			C2_FORMATARG('Q', "Minimum TM Receive queue length",
				     "%i", &cctx->cc_recv_queue_min_length),
			C2_FORMATARG('M', "Maximum RPC message size", "%i",
				     &cctx->cc_max_rpc_msg_size),
			C2_VOIDARG('r', "Start request handler",
				LAMBDA(void, (void)
				{
					rctx = NULL;
					rctx = cs_reqh_ctx_alloc(cctx);
					if (rctx == NULL) {
						rc = -ENOMEM;
						return;
					}
					rctx->rc_snr = 0;
				})),
			C2_STRINGARG('T', "Storage domain type",
				LAMBDA(void, (const char *str)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					rctx->rc_stype = str;
				})),
			C2_STRINGARG('D', "Database environment path",
				LAMBDA(void, (const char *str)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					rctx->rc_dbpath = str;
				})),
			C2_STRINGARG('S', "Storage domain name",
				LAMBDA(void, (const char *str)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					rctx->rc_stpath = str;
				})),
			C2_STRINGARG('d', "device configuration file",
				LAMBDA(void, (const char *str)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					rctx->rc_dfilepath = str;
				})),
			C2_STRINGARG('e',
				     "Network endpoint, eg:- transport:address",
			LAMBDA(void, (const char *str)
				{
					struct cs_endpoint_and_xprt *ep_xprt;
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					rc = ep_and_xprt_get(rctx, str,
							     &ep_xprt);
				})),
			C2_NUMBERARG('q', "Minimum TM recv queue length",
				LAMBDA(void, (int64_t length)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					rctx->rc_recv_queue_min_length = length;
				})),
			C2_NUMBERARG('m', "Maximum RPC message size",
				LAMBDA(void, (int64_t size)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					rctx->rc_max_rpc_msg_size = size;
				})),
			C2_STRINGARG('s', "Services to be configured",
				LAMBDA(void, (const char *str)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
					if (rctx->rc_snr >=
						rctx->rc_max_services) {
						rc = -E2BIG;
						C2_ADDB_ADD(&cctx->cc_addb,
							    &cs_addb_loc,
							    arg_fail,
							    "Too many services",
							    rc);
						return;
					}
					rctx->rc_services[rctx->rc_snr] = str;
					C2_CNT_INC(rctx->rc_snr);
				})));

        return result ?: rc;
}

int c2_cs_setup_env(struct c2_colibri *cctx, int argc, char **argv)
{
	int rc;

	C2_PRE(cctx != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	c2_rwlock_write_lock(&cctx->cc_rwlock);
	rc = cs_parse_args(cctx, argc, argv) ?:
		reqh_ctxs_are_valid(cctx) ?:
		cs_net_domains_init(cctx) ?:
		cs_buffer_pool_setup(cctx) ?:
		cs_request_handlers_start(cctx) ?:
		cs_rpc_machines_init(cctx);
	c2_rwlock_write_unlock(&cctx->cc_rwlock);

	if (rc < 0) {
		C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
			    setup_fail,
			    "c2_cs_setup_env", rc);
		cs_usage(cctx->cc_outfile);
	}

	return rc;
}

int c2_cs_start(struct c2_colibri *cctx)
{
	int rc;

	C2_PRE(cctx != NULL);

	rc = cs_services_init(cctx);
	if (rc != 0)
		C2_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
			    service_init_fail,
			    "c2_cs_start", rc);
	return rc;
}

int c2_cs_init(struct c2_colibri *cctx, struct c2_net_xprt **xprts,
	       size_t xprts_nr, FILE *out)
{
        C2_PRE(cctx != NULL && xprts != NULL && xprts_nr > 0 && out != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

        cctx->cc_xprts = xprts;
	cctx->cc_xprts_nr = xprts_nr;
	cctx->cc_outfile = out;
	cs_colibri_init(cctx);

	return 0;
}

void c2_cs_fini(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);

        cs_request_handlers_stop(cctx);
	cs_buffer_pool_fini(cctx);
	cs_net_domains_fini(cctx);
        cs_colibri_fini(cctx);
}

/** @} endgroup colibri_setup */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
