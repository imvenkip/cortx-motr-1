/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
#include <unistd.h>    /* daemon */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0D
#include "lib/trace.h"

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/finject.h"    /* M0_FI_ENABLED */
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/uuid.h"
#include "lib/locality.h"
#include "balloc/balloc.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "mgmt/mgmt.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "reqh/reqh.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h"
#include "mero/setup.h"
#include "mero/setup_internal.h"
#include "mero/magic.h"
#include "mero/version.h"
#include "rpc/rpclib.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup m0d
   @{
 */

/**
   Represents cob domain id, it is incremented for every new cob domain.

   @todo Have a generic mechanism to generate unique cob domain id.
   @todo Handle error messages properly
 */
static int cdom_id;

M0_TL_DESCR_DEFINE(cs_buffer_pools, "buffer pools in the mero context",
		   static, struct cs_buffer_pool, cs_bp_linkage, cs_bp_magic,
		   M0_CS_BUFFER_POOL_MAGIC, M0_CS_BUFFER_POOL_HEAD_MAGIC);
M0_TL_DEFINE(cs_buffer_pools, static, struct cs_buffer_pool);

M0_TL_DESCR_DEFINE(cs_eps, "cs endpoints", , struct cs_endpoint_and_xprt,
		   ex_linkage, ex_magix, M0_CS_ENDPOINT_AND_XPRT_MAGIC,
		   M0_CS_EPS_HEAD_MAGIC);

M0_TL_DEFINE(cs_eps, M0_INTERNAL, struct cs_endpoint_and_xprt);

static struct m0_bob_type cs_eps_bob;
M0_BOB_DEFINE(extern, &cs_eps_bob, cs_endpoint_and_xprt);

M0_INTERNAL const char *m0_cs_stypes[M0_STOB_TYPE_NR] = {
	[M0_LINUX_STOB] = "Linux",
	[M0_AD_STOB]    = "AD"
};

M0_INTERNAL const struct m0_stob_id m0_addb_stob_id = {
	.si_bits = {
		.u_hi = M0_ADDB_STOB_ID_HI,
		.u_lo = M0_ADDB_STOB_ID_LI
	}
};

M0_TL_DESCR_DEFINE(rhctx, "reqh contexts", static, struct m0_reqh_context,
		   rc_linkage, rc_magix, M0_CS_REQH_CTX_MAGIC,
		   M0_CS_REQH_CTX_HEAD_MAGIC);

M0_TL_DEFINE(rhctx, static, struct m0_reqh_context);

static struct m0_bob_type rhctx_bob;
M0_BOB_DEFINE(static, &rhctx_bob, m0_reqh_context);

M0_TL_DESCR_DEFINE(ndom, "network domains", static, struct m0_net_domain,
		   nd_app_linkage, nd_magix, M0_NET_DOMAIN_MAGIC,
		   M0_CS_NET_DOMAIN_HEAD_MAGIC);

M0_TL_DEFINE(ndom, static, struct m0_net_domain);

static struct m0_bob_type ndom_bob;
M0_BOB_DEFINE(static, &ndom_bob, m0_net_domain);

M0_TL_DESCR_DEFINE(astob, "ad stob domains", static, struct cs_ad_stob,
		   as_linkage, as_magix, M0_CS_AD_STOB_MAGIC,
		   M0_CS_AD_STOB_HEAD_MAGIC);
M0_TL_DEFINE(astob, static, struct cs_ad_stob);

static struct m0_bob_type astob_bob;
M0_BOB_DEFINE(static, &astob_bob, cs_ad_stob);

static bool streq(const char *s1, const char *s2)
{
	return strcmp(s1, s2) == 0;
}

/**
 * Returns true iff there is service with given name among rctx->rc_services.
 */
static bool
contains_service(const struct m0_reqh_context *rctx, const char *name)
{
	int i;

	for (i = 0; i < rctx->rc_nr_services; ++i) {
		if (streq(rctx->rc_services[i], name))
			return true;
	}
	return false;
}

static int reqh_ctx_args_are_valid(const struct m0_reqh_context *rctx)
{
	return equi(rctx->rc_confdb != NULL, contains_service(rctx, "confd")) &&
		rctx->rc_stype != NULL && rctx->rc_stpath != NULL &&
		rctx->rc_addb_stpath != NULL &&
		rctx->rc_dbpath != NULL && rctx->rc_nr_services != 0 &&
		rctx->rc_services != NULL &&
		!cs_eps_tlist_is_empty(&rctx->rc_eps);
}

M0_INTERNAL struct m0_rpc_machine *m0_mero_to_rmach(struct m0_mero *mero)
{
	struct m0_reqh_context *reqh_ctx;

	reqh_ctx = rhctx_tlist_head(&mero->cc_reqh_ctxs);
	if (reqh_ctx == NULL)
		return NULL;

	return m0_reqh_rpc_mach_tlist_head(&reqh_ctx->rc_reqh.rh_rpc_machines);
}

/**
   Checks consistency of request handler context.
 */
static bool m0_reqh_context_invariant(const struct m0_reqh_context *rctx)
{
	return m0_reqh_context_bob_check(rctx) &&
	       M0_IN(rctx->rc_state, (RC_UNINITIALISED, RC_INITIALISED)) &&
	       rctx->rc_max_services == m0_reqh_service_types_length() &&
	       M0_CHECK_EX(m0_tlist_invariant(&cs_eps_tl, &rctx->rc_eps)) &&
	       reqh_ctx_args_are_valid(rctx) && rctx->rc_mero != NULL &&
	       ergo(rctx->rc_state == RC_INITIALISED,
	       m0_reqh_invariant(&rctx->rc_reqh));
}

/**
   Looks up an xprt by the name.

   @param xprt_name Network transport name
   @param xprts Array of network transports supported in a mero environment
   @param xprts_nr Size of xprts array

   @pre xprt_name != NULL && xprts != NULL && xprts_nr > 0

 */
static struct m0_net_xprt *cs_xprt_lookup(const char *xprt_name,
					  struct m0_net_xprt **xprts,
					  size_t xprts_nr)
{
	size_t i;

	M0_PRE(xprt_name != NULL && xprts != NULL && xprts_nr > 0);

	for (i = 0; i < xprts_nr; ++i)
		if (strcmp(xprt_name, xprts[i]->nx_name) == 0)
			return xprts[i];
	return NULL;
}

/**
   Lists supported network transports.
 */
static void cs_xprts_list(FILE *out, struct m0_net_xprt **xprts,
			  size_t xprts_nr)
{
	int i;

	M0_PRE(out != NULL && xprts != NULL);

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

	M0_PRE(out != NULL);

	fprintf(out, "\nSupported stob types:\n");
	for (i = 0; i < ARRAY_SIZE(m0_cs_stypes); ++i)
		fprintf(out, " %s\n", m0_cs_stypes[i]);
}

/**
   Checks if the specified storage type is supported in a mero context.

   @param stype Storage type

   @pre stype != NULL
 */
static bool stype_is_valid(const char *stype)
{
	M0_PRE(stype != NULL);

	return  strcasecmp(stype, m0_cs_stypes[M0_AD_STOB]) == 0 ||
		strcasecmp(stype, m0_cs_stypes[M0_LINUX_STOB]) == 0;
}

/**
   Checks if given network transport and network endpoint address are already
   in use in a request handler context.

   @param cctx Mero context
   @param xprt Network transport
   @param ep Network end point address

   @pre cctx != NULL && xprt != NULL && ep != NULL
 */
static bool cs_endpoint_is_duplicate(struct m0_mero *cctx,
				     const struct m0_net_xprt *xprt,
				     const char *ep)
{
	int                          cnt;
	struct m0_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep_xprt;

	M0_PRE(cctx != NULL && xprt != NULL && ep != NULL);

	cnt = 0;
	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(m0_reqh_context_invariant(rctx));
		m0_tl_for(cs_eps, &rctx->rc_eps, ep_xprt) {
			M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep_xprt));
			if (strcmp(xprt->nx_name, "lnet") == 0) {
				if (m0_net_lnet_ep_addr_net_cmp(
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
		} m0_tl_endfor;
	} m0_tl_endfor;

	return false;
}

/**
   Checks if given network endpoint address and network transport are valid
   and if they are already in use in given mero context.

   @param cctx Mero context
   @param ep Network endpoint address
   @param xprt_name Network transport name

   @pre cctx != NULL && ep != NULL && xprt_name != NULL

   @retval 0 On success
	-EINVAL If endpoint is invalid
	-EADDRINUSE If endpoint is already in use
*/
static int cs_endpoint_validate(struct m0_mero *cctx, const char *ep,
				const char *xprt_name)
{
	struct m0_net_xprt *xprt;

	M0_ENTRY();
	M0_PRE(cctx != NULL);

	if (ep == NULL || xprt_name == NULL)
		M0_RETURN(-EINVAL);

	xprt = cs_xprt_lookup(xprt_name, cctx->cc_xprts, cctx->cc_xprts_nr);
	if (xprt == NULL)
		M0_RETURN(-EINVAL);

	M0_RETURN(cs_endpoint_is_duplicate(cctx, xprt, ep) ? -EADDRINUSE : 0);
}

int ep_and_xprt_extract(struct cs_endpoint_and_xprt *epx, const char *ep)
{
	char *sptr;
	char *endpoint;
	int   ep_len = min32u(strlen(ep) + 1, CS_MAX_EP_ADDR_LEN);

	epx->ex_cep = ep;
	M0_ALLOC_ARR(epx->ex_scrbuf, ep_len);
	if (epx->ex_scrbuf == NULL) {
		M0_LOG(M0_ERROR, "malloc failed");
		return -ENOMEM;
	}

	strncpy(epx->ex_scrbuf, ep, ep_len);
	epx->ex_xprt = strtok_r(epx->ex_scrbuf, ":", &sptr);
	if (epx->ex_xprt == NULL)
		goto err;

	endpoint = strtok_r(NULL, "\0", &sptr);
	if (endpoint == NULL)
		goto err;

	epx->ex_endpoint = endpoint;
	cs_endpoint_and_xprt_bob_init(epx);
	cs_eps_tlink_init(epx);
	return 0;

err:
	m0_free(epx->ex_scrbuf);
	return -EINVAL;
}

/**
   Extracts network transport name and network endpoint address from given
   mero endpoint.
   Mero endpoint is of 2 parts network xprt:network endpoint.
 */
static int ep_and_xprt_append(struct m0_tl *head, const char *ep)
{
	struct cs_endpoint_and_xprt *epx;
	int                          rc;
	M0_PRE(ep != NULL);

	M0_ALLOC_PTR(epx);
	if (epx == NULL) {
		M0_LOG(M0_ERROR, "malloc failed");
		return -ENOMEM;
	}

	rc = ep_and_xprt_extract(epx, ep);
	if (rc != 0)
		goto err;

	cs_eps_tlist_add_tail(head, epx);
	return 0;
err:
	m0_free(epx);
	return -EINVAL;
}

/**
   Checks if specified service has already a duplicate entry in given request
   handler context.
 */
static bool service_is_duplicate(const struct m0_reqh_context *rctx,
				 const char *sname)
{
	int n;
	int i;

	M0_PRE(m0_reqh_context_invariant(rctx));

	for (i = 0, n = 0; i < rctx->rc_nr_services; ++i) {
		if (strcasecmp(rctx->rc_services[i], sname) == 0)
			++n;
		if (n > 1)
			return true;
	}
	return false;
}

/**
   Allocates a request handler and adds it to the list of the same in given
   mero context.

   @param cctx Mero context

   @see m0_mero
 */
static struct m0_reqh_context *cs_reqh_ctx_alloc(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx;

	M0_PRE(cctx != NULL);

	M0_ALLOC_PTR(rctx);
	if (rctx == NULL) {
		M0_LOG(M0_ERROR, "malloc failed");
		return NULL;
	}

	rctx->rc_max_services = m0_reqh_service_types_length();
	if (rctx->rc_max_services == 0) {
		M0_LOG(M0_ERROR, "No services available");
		goto err;
	}

	M0_ALLOC_ARR(rctx->rc_services, rctx->rc_max_services);
	if (rctx->rc_services == NULL) {
		M0_LOG(M0_ERROR, "malloc failed");
		goto err;
	}
	M0_ALLOC_ARR(rctx->rc_service_uuids, rctx->rc_max_services);
	if (rctx->rc_service_uuids == NULL) {
		M0_LOG(M0_ERROR, "malloc failed");
		goto err;
	}

	m0_reqh_context_bob_init(rctx);
	cs_eps_tlist_init(&rctx->rc_eps);
	rhctx_tlink_init_at_tail(rctx, &cctx->cc_reqh_ctxs);
	rctx->rc_mero = cctx;

	return rctx;
err:
	m0_free(rctx->rc_services);
	m0_free(rctx);
	return NULL;
}

static void cs_reqh_ctx_free(struct m0_reqh_context *rctx)
{
	struct cs_endpoint_and_xprt *ep;
	int                          i;

	M0_PRE(rctx != NULL);

	m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		M0_ASSERT(ep->ex_scrbuf != NULL);
		m0_free(ep->ex_scrbuf);
		cs_eps_tlink_del_fini(ep);
		cs_endpoint_and_xprt_bob_fini(ep);
		m0_free(ep);
	} m0_tl_endfor;

	cs_eps_tlist_fini(&rctx->rc_eps);
	for (i = 0; i < rctx->rc_max_services; ++i)
		m0_free(rctx->rc_services[i]);
	m0_free(rctx->rc_services);
	m0_free(rctx->rc_service_uuids);
	rhctx_tlink_del_fini(rctx);
	m0_reqh_context_bob_fini(rctx);
	m0_free(rctx);
}

M0_INTERNAL struct m0_net_domain *
m0_cs_net_domain_locate(struct m0_mero *cctx, const char *xprt_name)
{
	struct m0_net_domain *ndom;

	M0_PRE(cctx != NULL && xprt_name != NULL);

	m0_tl_for(ndom, &cctx->cc_ndoms, ndom) {
		M0_ASSERT(m0_net_domain_bob_check(ndom));
		if (strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0)
			break;
	} m0_tl_endfor;

	return ndom;
}

static struct m0_net_buffer_pool *
cs_buffer_pool_get(struct m0_mero *cctx, struct m0_net_domain *ndom)
{
	struct cs_buffer_pool *cs_bp;

	M0_PRE(cctx != NULL);
	M0_PRE(ndom != NULL);

	m0_tl_for(cs_buffer_pools, &cctx->cc_buffer_pools, cs_bp) {
		if (cs_bp->cs_buffer_pool.nbp_ndom == ndom)
			return &cs_bp->cs_buffer_pool;
	} m0_tl_endfor;
	return NULL;
}

/**
   Initialises rpc machine for the given endpoint address.
   Once the new rpc_machine is created it is added to list of rpc machines
   in given request handler.
   Request handler should be initialised before invoking this function.

   @param cctx Mero context
   @param xprt_name Network transport
   @param ep Network endpoint address
   @param tm_colour Unique colour to be assigned to each TM in a domain
   @param recv_queue_min_length Minimum number of buffers in TM receive queue
   @param max_rpc_msg_size Maximum RPC message size
   @param reqh Request handler to which the newly created
		rpc_machine belongs

   @pre cctx != NULL && xprt_name != NULL && ep != NULL && reqh != NULL
 */
static int cs_rpc_machine_init(struct m0_mero *cctx, const char *xprt_name,
			       const char *ep, const uint32_t tm_colour,
			       const uint32_t recv_queue_min_length,
			       const uint32_t max_rpc_msg_size,
			       struct m0_reqh *reqh)
{
	struct m0_rpc_machine        *rpcmach;
	struct m0_net_domain         *ndom;
	struct m0_net_buffer_pool    *buffer_pool;
	int                           rc;

	M0_PRE(cctx != NULL && xprt_name != NULL && ep != NULL && reqh != NULL);

	ndom = m0_cs_net_domain_locate(cctx, xprt_name);
	if (ndom == NULL)
		return -EINVAL;
	if (max_rpc_msg_size > m0_net_domain_get_max_buffer_size(ndom))
		return -EINVAL;

	M0_ALLOC_PTR(rpcmach);
	if (rpcmach == NULL)
		return -ENOMEM;

	buffer_pool = cs_buffer_pool_get(cctx, ndom);
	rc = m0_rpc_machine_init(rpcmach, ndom, ep,
				 reqh, buffer_pool, tm_colour, max_rpc_msg_size,
				 recv_queue_min_length);
	if (rc == 0)
		m0_reqh_rpc_mach_tlink_init_at_tail(rpcmach,
						    &reqh->rh_rpc_machines);
	else
		m0_free(rpcmach);
	return rc;
}

/**
   Intialises rpc machines in a mero context.

   @param cctx Mero context
 */
static int cs_rpc_machines_init(struct m0_mero *cctx)
{
	int                          rc = 0;
	struct m0_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;

	M0_PRE(cctx != NULL);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(m0_reqh_context_invariant(rctx));
		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_rpc_machine_init(cctx, ep->ex_xprt,
						 ep->ex_endpoint,
						 ep->ex_tm_colour,
						 rctx->rc_recv_queue_min_length,
						 rctx->rc_max_rpc_msg_size,
						 &rctx->rc_reqh);
			if (rc != 0) {
				M0_LOG(M0_ERROR, "rpc_init_fail");
				return rc;
			}
		} m0_tl_endfor;
	} m0_tl_endfor;

	return rc;
}

/**
   Finalises all the rpc machines from the list of rpc machines present in
   m0_reqh.

   @param reqh Request handler of which the rpc machines belong

   @pre reqh != NULL
 */
static void cs_rpc_machines_fini(struct m0_reqh *reqh)
{
	struct m0_rpc_machine *rpcmach;

	M0_PRE(reqh != NULL);

	m0_tl_for(m0_reqh_rpc_mach, &reqh->rh_rpc_machines, rpcmach) {
		M0_ASSERT(m0_rpc_machine_bob_check(rpcmach));
		m0_reqh_rpc_mach_tlink_del_fini(rpcmach);
		m0_rpc_machine_fini(rpcmach);
		m0_free(rpcmach);
	} m0_tl_endfor;
}

static uint32_t cs_domain_tms_nr(struct m0_mero *cctx,
				struct m0_net_domain *dom)
{
	struct m0_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	uint32_t                     cnt = 0;

	M0_PRE(cctx != NULL);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			if (strcmp(ep->ex_xprt, dom->nd_xprt->nx_name) == 0)
				ep->ex_tm_colour = cnt++;
		} m0_tl_endfor;
	} m0_tl_endfor;

	M0_POST(cnt > 0);
	return cnt;
}

/**
   It calculates the summation of the minimum receive queue length of all
   endpoints belong to a domain in all the reqest handler contexts.
 */
static uint32_t cs_dom_tm_min_recv_queue_total(struct m0_mero *cctx,
					       struct m0_net_domain *dom)
{
	struct m0_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	uint32_t                     min_queue_len_total = 0;

	M0_PRE(cctx != NULL);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(m0_reqh_context_bob_check(rctx));
		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			if (strcmp(ep->ex_xprt, dom->nd_xprt->nx_name) == 0)
				min_queue_len_total +=
					rctx->rc_recv_queue_min_length;
		} m0_tl_endfor;
	} m0_tl_endfor;
	return min_queue_len_total;
}

static void cs_buffer_pool_fini(struct m0_mero *cctx)
{
	struct cs_buffer_pool   *cs_bp;

	M0_PRE(cctx != NULL);

	m0_tl_for(cs_buffer_pools, &cctx->cc_buffer_pools, cs_bp) {
		cs_buffer_pools_tlink_del_fini(cs_bp);
		m0_net_buffer_pool_fini(&cs_bp->cs_buffer_pool);
		m0_free(cs_bp);
	} m0_tl_endfor;
}

static int cs_buffer_pool_setup(struct m0_mero *cctx)
{
	int                    rc = 0;
	struct m0_net_domain  *dom;
	struct cs_buffer_pool *cs_bp;
	uint32_t               tms_nr;
	uint32_t               bufs_nr;
	uint32_t               max_recv_queue_len;

	M0_PRE(cctx != NULL);

	m0_tl_for(ndom, &cctx->cc_ndoms, dom) {
		max_recv_queue_len = cs_dom_tm_min_recv_queue_total(cctx, dom);
		tms_nr		   = cs_domain_tms_nr(cctx, dom);
		M0_ASSERT(max_recv_queue_len >= tms_nr);

		M0_ALLOC_PTR(cs_bp);
		if (cs_bp == NULL) {
			rc = -ENOMEM;
			break;
		}

		bufs_nr = m0_rpc_bufs_nr(max_recv_queue_len, tms_nr);
		rc = m0_rpc_net_buffer_pool_setup(dom, &cs_bp->cs_buffer_pool,
						  bufs_nr, tms_nr);
		if (rc != 0) {
			m0_free(cs_bp);
			break;
		}
		cs_buffer_pools_tlink_init_at_tail(cs_bp,
						   &cctx->cc_buffer_pools);
	} m0_tl_endfor;

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

static int cs_stob_file_load(const char *dfile, struct cs_stobs *stob)
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
			     struct m0_be_seg *db, const char *f_path)
{
	int                 rc;
	char                ad_dname[MAXPATHLEN];
	struct m0_stob_id  *bstob_id;
	struct m0_stob    **bstob;
	struct m0_balloc   *cb;
	struct cs_ad_stob  *adstob;
	struct m0_sm_group *grp = m0_locality0_get()->lo_grp;

	M0_ENTRY("cid=%d path=%s", (int)cid, f_path);

	M0_ALLOC_PTR(adstob);
	if (adstob == NULL) {
		M0_LOG(M0_ERROR, "malloc failed");
		M0_RETURN(-ENOMEM);
	}

	bstob = &adstob->as_stob_back;
	bstob_id = &adstob->as_id_back;
	bstob_id->si_bits.u_hi = cid;
	bstob_id->si_bits.u_lo = M0_AD_STOB_ID_LO;
	rc = m0_stob_find(stob->s_ldom, bstob_id, bstob);
	if (rc == 0) {
		if (f_path != NULL)
			rc = m0_linux_stob_link(stob->s_ldom, *bstob,
						f_path, NULL);
		if (rc == 0 || rc == -EEXIST)
			rc = m0_stob_create_helper(stob->s_ldom, NULL,
						bstob_id, bstob);
		if (rc == 0)
			m0_stob_put(*bstob);
	}

	if (rc == 0) {
		sprintf(ad_dname, "%lx%lx", bstob_id->si_bits.u_hi,
					    bstob_id->si_bits.u_lo);
		m0_sm_group_lock(grp);
		rc = m0_ad_stob_domain_locate(ad_dname, db, grp,
					      &adstob->as_dom, *bstob);

		if (rc == 0 && M0_FI_ENABLED("ad_domain_locate_fail")) {
			struct m0_stob_domain *adom = adstob->as_dom;
			adom->sd_ops->sdo_fini(adom, grp);
			rc = -EINVAL;
		}

		if (rc == 0) {
			cs_ad_stob_bob_init(adstob);
			astob_tlink_init_at_tail(adstob, &stob->s_adstobs);
			rc =	m0_balloc_create(cid, db, grp, &cb) ?:
				m0_ad_stob_setup(adstob->as_dom, db, grp,
						 *bstob, &cb->cb_ballroom,
						 BALLOC_DEF_CONTAINER_SIZE,
						 BALLOC_DEF_BLOCK_SHIFT,
						 BALLOC_DEF_BLOCKS_PER_GROUP,
						 BALLOC_DEF_RESERVED_GROUPS);
			if (rc == 0 && M0_FI_ENABLED("ad_stob_setup_fail"))
				rc = -EINVAL;
			if (rc != 0) {
				struct m0_stob_domain *adom = adstob->as_dom;
				astob_tlink_del_fini(adstob);
				cs_ad_stob_bob_fini(adstob);
				adom->sd_ops->sdo_fini(adom, grp);
			}
		}
		m0_sm_group_unlock(grp);

		if (rc != 0)
			m0_stob_put(*bstob);
	}

	if (rc != 0)
		m0_free(adstob);
	M0_RETURN(rc);
}

/**
   Initialises AD type stob.
 */
static int cs_ad_stob_init(struct cs_stobs *stob, struct m0_be_seg *db)
{
	int                rc;
	int                result;
	uint64_t           cid;
	const char        *f_path;
	yaml_document_t   *doc;
	yaml_node_t       *node;
	yaml_node_t       *s_node;
	yaml_node_item_t  *item;

	M0_ENTRY();

	M0_PRE(stob != NULL);

	astob_tlist_init(&stob->s_adstobs);
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
				rc = cs_ad_stob_create(stob, cid, db, f_path);
				if (rc != 0)
					break;
			}
		}
	} else {
		rc = cs_ad_stob_create(stob, M0_AD_STOB_ID_DEFAULT, db, NULL);
	}

	M0_RETURN(rc);
}

/**
   Initialises linux type stob.
 */
static int cs_linux_stob_init(const char *stob_path, struct cs_stobs *stob)
{
	return m0_linux_stob_domain_locate(stob_path, &stob->s_ldom) ?:
	       m0_linux_stob_setup(stob->s_ldom, false);
}

static void cs_ad_stob_fini(struct cs_stobs *stob)
{
	struct m0_sm_group    *grp = m0_locality0_get()->lo_grp;
	struct m0_stob        *bstob;
	struct cs_ad_stob     *adstob;
	struct m0_stob_domain *adom;

	M0_PRE(stob != NULL);

	m0_tl_for(astob, &stob->s_adstobs, adstob) {
		M0_ASSERT(cs_ad_stob_bob_check(adstob) &&
			  adstob->as_dom != NULL);
		bstob = adstob->as_stob_back;
		adom = adstob->as_dom;
		if (bstob != NULL && bstob->so_state == CSS_EXISTS)
			m0_stob_put(bstob);

		m0_sm_group_lock(grp);
		adom->sd_ops->sdo_fini(adom, grp);
		m0_sm_group_unlock(grp);

		astob_tlink_del_fini(adstob);
		cs_ad_stob_bob_fini(adstob);
		m0_free(adstob);
	} m0_tl_endfor;
	astob_tlist_fini(&stob->s_adstobs);
}

static void cs_linux_stob_fini(struct cs_stobs *stob)
{
	M0_PRE(stob != NULL);

	if (stob->s_ldom != NULL)
		stob->s_ldom->sd_ops->sdo_fini(stob->s_ldom, NULL);
}

M0_INTERNAL struct m0_stob_domain *m0_cs_stob_domain_find(struct m0_reqh *reqh,
							  const struct
							  m0_stob_id *stob_id)
{
	struct m0_reqh_context  *rqctx;
	struct cs_stobs         *stob;
	struct cs_ad_stob       *adstob;

	rqctx = bob_of(reqh, struct m0_reqh_context, rc_reqh, &rhctx_bob);
	stob = &rqctx->rc_stob;

	if (stob->s_stype == M0_LINUX_STOB)
		return stob->s_ldom;
	else if (stob->s_stype == M0_AD_STOB) {
		m0_tl_for(astob, &stob->s_adstobs, adstob) {
			M0_ASSERT(cs_ad_stob_bob_check(adstob));
			if (!stob->s_sfile.sf_is_initialised ||
			    adstob->as_id_back.si_bits.u_hi ==
			    stob_id->si_bits.u_hi)
				return adstob->as_dom;
		} m0_tl_endfor;
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
			   struct cs_stobs *stob, struct m0_be_seg *db)
{
	int               rc;
	int               slen;
	char             *objpath;
	static const char objdir[] = "/o";

	M0_ENTRY();

	M0_PRE(stob_type != NULL && stob_path != NULL && stob != NULL);

	if (strcasecmp(stob_type, m0_cs_stypes[M0_LINUX_STOB]) == 0)
		stob->s_stype = M0_LINUX_STOB;
	else if (strcasecmp(stob_type, m0_cs_stypes[M0_AD_STOB]) == 0)
		stob->s_stype = M0_AD_STOB;
	else
		M0_RETURN(-EINVAL);

	slen = strlen(stob_path);
	M0_ALLOC_ARR(objpath, slen + ARRAY_SIZE(objdir));
	if (objpath == NULL)
		M0_RETURN(-ENOMEM);

	sprintf(objpath, "%s%s", stob_path, objdir);

	rc = mkdir(stob_path, 0700);
	if (rc != 0 && errno != EEXIST) {
		M0_LOG(M0_ERROR, "mkdir(%s) failed: %d", stob_path, errno);
		goto out;
	}

	rc = mkdir(objpath, 0700);
	if (rc != 0 && errno != EEXIST) {
		M0_LOG(M0_ERROR, "mkdir(%s) failed: %d", objpath, errno);
		goto out;
	}

	rc = cs_linux_stob_init(stob_path, stob);
	if (rc == 0 && strcasecmp(stob_type, m0_cs_stypes[M0_AD_STOB]) == 0)
		rc = cs_ad_stob_init(stob, db);
out:
	m0_free(objpath);

	M0_RETURN(rc);
}

/**
   Finalises storage for a request handler in a mero context.
 */
static void cs_storage_fini(struct cs_stobs *stob)
{
	M0_PRE(stob != NULL);

	if (stob->s_stype == M0_AD_STOB)
		cs_ad_stob_fini(stob);
	cs_linux_stob_fini(stob);
	if (stob->s_sfile.sf_is_initialised)
		yaml_document_delete(&stob->s_sfile.sf_document);
}

/**
   Initialises and starts a particular service.

   Once the service is initialised, it is started and registered with the
   appropriate request handler.
 */
static int
cs_service_init(const char *name, struct m0_reqh_context *rctx,
		struct m0_reqh *reqh, struct m0_uint128 *uuid)
{
	struct m0_reqh_service_type *stype;
	struct m0_reqh_service      *service;
	int                          rc;

	M0_ENTRY("name=`%s'", name);
	M0_PRE(name != NULL && *name != '\0' && reqh != NULL);

	stype = m0_reqh_service_type_find(name);
	if (stype == NULL)
		M0_RETURN(-EINVAL);

	rc = m0_reqh_service_allocate(&service, stype, rctx);
	if (rc != 0)
		M0_RETURN(rc);

	m0_reqh_service_init(service, reqh, uuid);

	/** @todo Remove the USE_MGMT_STARTUP macro later */
#define USE_MGMT_STARTUP 1
#if USE_MGMT_STARTUP
	rc = m0_mgmt_reqh_service_start(service);
#else
	rc = m0_reqh_service_start(service);
#endif
	if (rc != 0)
		m0_reqh_service_fini(service);

	M0_POST(ergo(rc == 0, m0_reqh_service_invariant(service)));
	M0_RETURN(rc);
}

static int reqh_services_init(struct m0_reqh_context *rctx)
{
	const char *name;
	uint32_t    i;
	int         rc;

	M0_ENTRY();
	M0_PRE(m0_reqh_context_invariant(rctx));

	for (i = 0, rc = 0; i < rctx->rc_nr_services && rc == 0; ++i) {
		name = rctx->rc_services[i];
		rc = cs_service_init(name, rctx, &rctx->rc_reqh,
				     &rctx->rc_service_uuids[i]);
	}
#if USE_MGMT_STARTUP
	/* Do not terminate on failure here as services start asynchronously. */
#else
	if (rc != 0)
		m0_reqh_services_terminate(&rctx->rc_reqh);
#endif
	M0_RETURN(rc);
}

/**
   Initialises set of services specified in a request handler context.
   Services are started once the mero context is configured successfully
   which includes network domains, request handlers, and rpc machines.

   @param cctx Mero context
 */
static int cs_services_init(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx;
	int                     rc = 0;

	M0_ENTRY();
	M0_PRE(cctx != NULL);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
#if USE_MGMT_STARTUP
		rc = m0_reqh_mgmt_service_start(&rctx->rc_reqh);
		if (rc != 0)
			break;
		m0_reqh_start(&rctx->rc_reqh);
		rc = cs_service_init("rpcservice", NULL, &rctx->rc_reqh,
				     NULL) ?:
			reqh_services_init(rctx);
		m0_mgmt_reqh_services_start_wait(&rctx->rc_reqh);
		/* return failure if any service has failed */
		if (rc == 0 &&
		    m0_reqh_services_state_count(&rctx->rc_reqh,
						 M0_RST_FAILED) > 0)
			rc = -ENOEXEC;
		if (rc != 0)
			break;
#else
		rc = m0_reqh_mgmt_service_start(&rctx->rc_reqh) ?:
			cs_service_init("rpcservice", NULL, &rctx->rc_reqh,
					NULL) ?:
			reqh_services_init(rctx);
		if (rc != 0)
			break;
		m0_reqh_start(&rctx->rc_reqh);
#endif
	} m0_tl_endfor;

	M0_RETURN(rc);
}

/**
   Initialises network domains per given distinct xport:endpoint pair in a
   mero context.

   @param cctx Mero context
 */
static int cs_net_domains_init(struct m0_mero *cctx)
{
	size_t                       xprts_nr;
	struct m0_net_xprt         **xprts;
	struct m0_net_xprt          *xprt;
	struct m0_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	struct m0_net_domain        *ndom;
	int                          rc = 0;

	M0_ENTRY();
	M0_PRE(cctx != NULL);

	xprts = cctx->cc_xprts;
	xprts_nr = cctx->cc_xprts_nr;

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(m0_reqh_context_invariant(rctx));

		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));

			xprt = cs_xprt_lookup(ep->ex_xprt, xprts, xprts_nr);
			if (xprt == NULL) {
				M0_LOG(M0_ERROR, "endpoint_init_fail");
				M0_RETURN(-EINVAL);
			}

			ndom = m0_cs_net_domain_locate(cctx, ep->ex_xprt);
			if (ndom != NULL)
				continue;

			rc = m0_net_xprt_init(xprt);
			if (rc != 0)
				M0_RETURN(rc);

			M0_ALLOC_PTR(ndom);
			if (ndom == NULL) {
				M0_LOG(M0_ERROR, "malloc failed");
				m0_net_xprt_fini(xprt);
				M0_RETURN(-ENOMEM);
			}
			/** @todo replace m0_addb_proc_ctx */
			rc = m0_net_domain_init(ndom, xprt, &m0_addb_proc_ctx);
			if (rc != 0) {
				m0_free(ndom);
				m0_net_xprt_fini(xprt);
				M0_RETURN(rc);
			}

			m0_net_domain_bob_init(ndom);
			ndom_tlink_init_at_tail(ndom, &cctx->cc_ndoms);
		} m0_tl_endfor;
	} m0_tl_endfor;

	M0_RETURN(rc);
}

/**
   Finalises all the network domains within a mero context.

   @param cctx Mero context to which the network domains belong
 */
static void cs_net_domains_fini(struct m0_mero *cctx)
{
	struct m0_net_domain  *ndom;
	struct m0_net_xprt   **xprts;
	size_t                 idx;

	M0_PRE(cctx != NULL);

	xprts = cctx->cc_xprts;
	m0_tl_for(ndom, &cctx->cc_ndoms, ndom) {
		M0_ASSERT(m0_net_domain_bob_check(ndom));
		m0_net_domain_fini(ndom);
		ndom_tlink_del_fini(ndom);
		m0_net_domain_bob_fini(ndom);
		m0_free(ndom);
	} m0_tl_endfor;

	for (idx = 0; idx < cctx->cc_xprts_nr; ++idx)
		m0_net_xprt_fini(xprts[idx]);
}

static int cs_storage_prepare(struct m0_reqh_context *rctx)
{
	struct m0_sm_group	*grp = m0_locality0_get()->lo_grp;
	struct m0_cob_domain	*dom = &rctx->rc_mdstore.md_dom;
	struct m0_dtx		 tx = {};
	int			 rc;

	m0_sm_group_lock(grp);
	rc = m0_mdstore_create(&rctx->rc_mdstore, grp);
	m0_sm_group_unlock(grp);
	if (rc != 0)
		return rc;

	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, rctx->rc_beseg->bs_domain, grp);
	m0_cob_tx_credit(dom, M0_COB_OP_DOMAIN_MKFS, &tx.tx_betx_cred);
	rc = m0_dtx_open_sync(&tx);
	if (rc == 0) {
		rc = m0_cob_domain_mkfs(dom, &M0_COB_SLASH_FID,
					     &M0_COB_SESSIONS_FID, &tx.tx_betx);
		if (rc != 0)
			m0_cob_domain_destroy(dom, grp);
		m0_dtx_done_sync(&tx);
	}
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);

	return rc;
}

/**
   Initializes storage for ADDB depending on the type of specified
   while running m0d. It also creates a hard-coded stob on
   top of the stob(linux/AD), that is passed to
   @see m0_addb_mc_configure_stob_sink() that is used by ADDB machine
   to store the ADDB recs.
 */
static int cs_addb_storage_init(struct m0_reqh_context *rctx)
{
	int                    rc;
	struct m0_dtx          tx = {};
	struct cs_ad_stob     *ad_stob;
	struct m0_stob_domain *sdom;
	struct cs_addb_stob   *addb_stob = &rctx->rc_addb_stob;
	struct m0_sm_group    *grp = m0_locality0_get()->lo_grp;

	M0_ENTRY();

	/** @todo allow different stob type for data stobs & ADDB stobs? */
	rc = cs_storage_init(rctx->rc_stype, rctx->rc_addb_stpath,
			     &addb_stob->cas_stobs, rctx->rc_beseg);
	if (rc != 0)
		M0_RETURN(rc);

	if (strcasecmp(rctx->rc_stype, m0_cs_stypes[M0_LINUX_STOB]) == 0) {
		sdom = rctx->rc_addb_stob.cas_stobs.s_ldom;
	} else {
		M0_ASSERT(!m0_tlist_is_empty(&astob_tl,
					     &addb_stob->cas_stobs.s_adstobs));
		ad_stob = astob_tlist_head(&addb_stob->cas_stobs.s_adstobs);
		M0_ASSERT(ad_stob != NULL);

		sdom = ad_stob->as_dom;
	}

	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, rctx->rc_beseg->bs_domain, grp);
	m0_stob_create_helper_credit(sdom, &m0_addb_stob_id, &tx.tx_betx_cred);
	rc = m0_dtx_open_sync(&tx);
	if (rc == 0) {
		rc = m0_stob_create_helper(sdom, &tx, &m0_addb_stob_id,
					   &addb_stob->cas_stob);
		m0_dtx_done_sync(&tx);
	}
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);

	M0_RETURN(rc);
}

/**
   Puts the reference of the hard-coded stob, and does the general fini
 */
static void cs_addb_storage_fini(struct cs_addb_stob *addb_stob)
{
	m0_stob_put(addb_stob->cas_stob);
	/* cs_storage_fini fini's the dom, which is shared with gmc */
	if (m0_addb_mc_is_initialized(&m0_addb_gmc) &&
	    m0_addb_mc_has_recsink(&m0_addb_gmc)) {
		m0_addb_mc_fini(&m0_addb_gmc);
		m0_addb_mc_init(&m0_addb_gmc);
	}
	cs_storage_fini(&addb_stob->cas_stobs);
}

/**
   Initialises a request handler context.
   A request handler context consists of the storage domain, database,
   cob domain, fol and request handler instance to be initialised.
   The request handler context is allocated and initialised per request handler
   in a mero process per node. So, there can exist multiple request handlers
   and thus multiple request handler contexts in a mero context.

   @param rctx Request handler context to be initialised
 */
static int cs_request_handler_start(struct m0_reqh_context *rctx)
{
	int rc;

	M0_ENTRY();

	/** @todo Pass in a parent ADDB context for the db. Ideally should
	    be same parent as that of the reqh.
	    But, we'd also want the db to use the same addb m/c as the reqh.
	    Needs work.
	 */

	rc = M0_REQH_INIT(&rctx->rc_reqh,
			  .rhia_dtm       = NULL,
			  .rhia_db        = NULL,
			  .rhia_mdstore   = &rctx->rc_mdstore,
			  .rhia_fol       = NULL,
			  .rhia_svc       = NULL);
	if (rc != 0)
		goto out;

	rc = m0_dbenv_init(&rctx->rc_db, rctx->rc_dbpath, 0);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_dbenv_init");
		return rc;
	}
	rctx->rc_beseg = rctx->rc_db.d_i.d_seg;
	rctx->rc_reqh.rh_dbenv = &rctx->rc_db;

	rc = m0_reqh_fol_create(&rctx->rc_reqh, rctx->rc_beseg);
	if (rc != 0 && rc != -EEXIST)
		goto reqh_fini;

	rc = m0_reqh_dbenv_init(&rctx->rc_reqh, rctx->rc_beseg);
	if (rc != 0)
		goto dbenv_fini;

	if (rctx->rc_dfilepath != NULL) {
		rc = cs_stob_file_load(rctx->rc_dfilepath, &rctx->rc_stob);
		if (rc != 0) {
			M0_LOG(M0_ERROR,
			       "Failed to load device configuration file");
			goto reqh_dbenv_fini;
		}
	}

	rc = cs_storage_init(rctx->rc_stype, rctx->rc_stpath,
			     &rctx->rc_stob, rctx->rc_beseg);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "cs_storage_init");
		goto cleanup_stob;
	}

	rc = cs_addb_storage_init(rctx);
	if (rc != 0)
		goto cleanup_stob;

	rc = m0_reqh_addb_mc_config(&rctx->rc_reqh,
				    rctx->rc_addb_stob.cas_stob);
	if (rc != 0)
		goto cleanup_addb_stob;

	rctx->rc_cdom_id.id = ++cdom_id;

	/** Mkfs cob domain before using it. */
	if (rctx->rc_prepare_storage) {
		/*
		 * Init mdstore without root cob init first. Now we can use its
		 * cob domain for mkfs.
		 */
		rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id,
				     rctx->rc_beseg, false);
		if (rc != 0 && rc != -ENOENT) {
			M0_LOG(M0_ERROR, "m0_mdstore_init: rc=%d", rc);
			goto cleanup_addb_stob;
		}
		if (rc == -ENOENT)
			rc = cs_storage_prepare(rctx);
		m0_mdstore_fini(&rctx->rc_mdstore);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "cs_storage_prepare: rc=%d", rc);
			goto cleanup_addb_stob;
		}
	}

	rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id,
			     rctx->rc_beseg, true);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_mdstore_init: rc=%d", rc);
		goto cleanup_addb_stob;
	}

	rctx->rc_state = RC_INITIALISED;
	M0_RETURN(rc);

cleanup_addb_stob:
	cs_addb_storage_fini(&rctx->rc_addb_stob);
cleanup_stob:
	cs_storage_fini(&rctx->rc_stob);
reqh_dbenv_fini:
	m0_reqh_fol_destroy(&rctx->rc_reqh);
	m0_reqh_dbenv_fini(&rctx->rc_reqh);
dbenv_fini:
	m0_dbenv_fini(&rctx->rc_db);
reqh_fini:
	m0_reqh_fini(&rctx->rc_reqh);
out:
	M0_ASSERT(rc != 0);
	M0_RETURN(rc);
}

/**
   Configures one or more request handler contexts and starts corresponding
   request handlers in each context.
 */
static int cs_request_handlers_start(struct m0_mero *cctx)
{
	int                     rc = 0;
	struct m0_reqh_context *rctx;

	M0_PRE(cctx != NULL);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(m0_reqh_context_invariant(rctx));
		rc = cs_request_handler_start(rctx);
		if (rc != 0)
			return rc;
	} m0_tl_endfor;

	return rc;
}

/**
   Finalises a request handler context.
   Sets m0_reqh::rh_shutdown true, and checks if the request handler can be
   shutdown by invoking m0_reqh_can_shutdown().
   This waits until m0_reqh_can_shutdown() returns true and then proceeds for
   further cleanup.

   @param rctx Request handler context to be finalised

   @pre m0_reqh_context_invariant()
 */
static void cs_request_handler_stop(struct m0_reqh_context *rctx)
{
	struct m0_reqh *reqh = &rctx->rc_reqh;

	M0_PRE(m0_reqh_context_invariant(rctx));

	M0_ENTRY();

	if (m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL)
		m0_reqh_shutdown(reqh);
	m0_reqh_fom_domain_idle_wait(reqh);

	if (m0_reqh_state_get(reqh) == M0_REQH_ST_DRAIN ||
	    m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED ||
	    m0_reqh_state_get(reqh) == M0_REQH_ST_INIT)
		m0_reqh_services_terminate(reqh);

	if (m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STOP)
		m0_reqh_mgmt_service_stop(reqh);
	M0_ASSERT(m0_reqh_state_get(reqh) == M0_REQH_ST_STOPPED);

	m0_reqh_dbenv_fini(reqh);
	m0_mdstore_fini(&rctx->rc_mdstore);
	cs_addb_storage_fini(&rctx->rc_addb_stob);
	cs_storage_fini(&rctx->rc_stob);
	m0_dbenv_fini(&rctx->rc_db);
	cs_rpc_machines_fini(reqh);
	m0_reqh_fini(reqh);
}

/**
   Finalises all the request handler contexts within a mero context.

   @param cctx Mero context to which the reqeust handler contexts belong
 */
static void cs_request_handlers_stop(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx;

	M0_PRE(cctx != NULL);

	M0_ENTRY();

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		if (rctx->rc_state == RC_INITIALISED)
			cs_request_handler_stop(rctx);
		cs_reqh_ctx_free(rctx);
	} m0_tl_endfor;
}

/**
   Find a request handler service within a given Mero instance.

   @param cctx Pointer to Mero context
   @param service_name Name of the service

   @pre cctx != NULL && service_name != NULL

   @retval  NULL of reqh instnace.
 */
struct m0_reqh *m0_cs_reqh_get(struct m0_mero *cctx,
			       const char *service_name)
{
	int                     i;
	struct m0_reqh_context *rctx;
	struct m0_reqh         *ret = NULL;

	M0_PRE(cctx != NULL);
	M0_PRE(service_name != NULL);

	m0_rwlock_read_lock(&cctx->cc_rwlock);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(m0_reqh_context_invariant(rctx));

		for (i = 0; i < rctx->rc_nr_services; ++i) {
			if (streq(rctx->rc_services[i], service_name)) {
				ret = &rctx->rc_reqh;
				goto out;
			}
		}
	} m0_tl_endfor;
out:
	m0_rwlock_read_unlock(&cctx->cc_rwlock);
	return ret;
}
M0_EXPORTED(m0_cs_reqh_get);

static struct m0_reqh_context *cs_reqh_ctx_get(struct m0_reqh *reqh)
{
	struct m0_reqh_context *rqctx;

	M0_PRE(m0_reqh_invariant(reqh));

	rqctx = bob_of(reqh, struct m0_reqh_context, rc_reqh, &rhctx_bob);
	M0_POST(m0_reqh_context_invariant(rqctx));

	return rqctx;
}

M0_INTERNAL struct m0_mero *m0_cs_ctx_get(struct m0_reqh *reqh)
{
	return cs_reqh_ctx_get(reqh)->rc_mero;
}

/**
   Initialises a mero context.

   @param cctx Mero context to be initialised

   @pre cctx != NULL
 */
static void cs_mero_init(struct m0_mero *cctx)
{
	M0_PRE(cctx != NULL);

	rhctx_tlist_init(&cctx->cc_reqh_ctxs);
	m0_bob_type_tlist_init(&rhctx_bob, &rhctx_tl);

	ndom_tlist_init(&cctx->cc_ndoms);
	m0_bob_type_tlist_init(&ndom_bob, &ndom_tl);
	cs_buffer_pools_tlist_init(&cctx->cc_buffer_pools);

	m0_bob_type_tlist_init(&cs_eps_bob, &cs_eps_tl);

	m0_bob_type_tlist_init(&astob_bob, &astob_tl);
	m0_rwlock_init(&cctx->cc_rwlock);

	cs_eps_tlist_init(&cctx->cc_ios_eps);
	cctx->cc_args.ca_argc = 0;
}

/**
   Finalises a mero context.

   @pre cctx != NULL
 */
static void cs_mero_fini(struct m0_mero *cctx)
{
	struct cs_endpoint_and_xprt *ep;
	M0_PRE(cctx != NULL);

	m0_tl_for(cs_eps, &cctx->cc_ios_eps, ep) {
		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		M0_ASSERT(ep->ex_scrbuf != NULL);
		m0_free(ep->ex_scrbuf);
		cs_eps_tlink_del_fini(ep);
		cs_endpoint_and_xprt_bob_fini(ep);
		m0_free(ep);
	} m0_tl_endfor;

	cs_eps_tlist_fini(&cctx->cc_ios_eps);

	rhctx_tlist_fini(&cctx->cc_reqh_ctxs);
	cs_buffer_pools_tlist_fini(&cctx->cc_buffer_pools);
	ndom_tlist_fini(&cctx->cc_ndoms);
	m0_rwlock_fini(&cctx->cc_rwlock);

	while (cctx->cc_args.ca_argc > 0)
		m0_free(cctx->cc_args.ca_argv[--cctx->cc_args.ca_argc]);
}

/**
   Displays usage of m0d program.

   @param out File to which the output is written
 */
static void cs_usage(FILE *out)
{
	M0_PRE(out != NULL);

	fprintf(out,
"Usage: m0d [-h] [-x] [-l]\n"
"    or m0d <global options> <reqh>+\n"
"\n"
"Type `m0d -h' for help.\n");
}

/**
   Displays help for m0d program.

   @param out File to which the output is written
 */
static void cs_help(FILE *out)
{
	M0_PRE(out != NULL);

	cs_usage(out);
	fprintf(out, "\n"
"Queries:\n"
"  -h   Display this help.\n"
"  -x   List supported network transports.\n"
"  -l   List supported services.\n"
"\n"
"Global options:\n"
"  -Q num   Minimum length of TM receive queue.\n"
"  -M num   Maximum RPC message size.\n"
"  -w num   Pool width.\n"
"  -C addr  Endpoint address of confd service.\n"
"  -P str   Configuration profile.\n"
"  -G addr  Endpoint address of mdservice.\n"
"  -i addr  Add new entry to the list of ioservice endpoint addresses.\n"
"  -f path  Path to genders file, defaults to /etc/mero/genders.\n"
"  -g       Bootstrap configuration using genders.\n"
"  -Z       Run as a daemon.\n"
"\n"
"Request handler options:\n"
"  -r   Start new set of request handler options.\n"
"       There may be multiple '-r' sets, each representing one request\n"
"       handler.\n"
"\n"
"  -p       Prepare storage (root session, root hierarchy, etc).\n"
"  -D str   Database environment path.\n"
"  -c str   [optional] Path to the configuration database.\n"
"  -T str   Type of storage. Supported types: linux, ad.\n"
"  -S str   Stob file.\n"
"  -A str   ADDB Stob file.\n"
"  -d str   [optional] Path to device configuration file.\n"
"           Device configuration file should contain device id and the\n"
"           corresponding device path.\n"
"           E.g. id: 0,\n"
"                filename: /dev/sda\n"
"           Note that only AD type stob domain can be configured over device.\n"
"  -e addr  Network layer endpoint of a service.\n"
"           Format: <transport>:<address>.\n"
"           Currently supported transport is lnet.\n"
"           .\n"
"           lnet takes 4-tuple endpoint address in the form\n"
"               NID : PID : PortalNumber : TransferMachineIdentifier\n"
"           e.g. lnet:172.18.50.40@o2ib1:12345:34:1\n"
"           .\n"
"           There may be several '-e' options in one set of request handler\n"
"           options.  In this case network transport will have several\n"
"           endpoints, distinguished by transfer machine id (the 4th\n"
"           component of 4-tuple endpoint address in lnet).\n"
"  -s str   Service (type) to be started in given request handler context.\n"
"           The string is of one of the following forms:\n"
"              ServiceTypeName:ServiceInstanceUUID\n"
"              ServiceTypeName\n"
"           with the UUID expressed in the standard 8-4-4-4-12 hexadecimal\n"
"           string form. The non-UUID form is permitted for testing purposes.\n"
"           There may be several '-s' options in one set of request handler\n"
"           options. Duplicated service type names are not allowed.\n"
"           Use '-l' to get a list of registered service types.\n"
"  -q num   [optional] Minimum length of TM receive queue.\n"
"           Defaults to the value set with '-Q' option.\n"
"  -m num   [optional] Maximum RPC message size.\n"
"           Defaults to the value set with '-M' option.\n"
"\n"
"Example:\n"
"    m0d -Q 4 -M 4096 -r -T linux -D dbpath -S stobfile \\\n"
"        -e lnet:172.18.50.40@o2ib1:12345:34:1 -s mds -q 8 -m 65536\n");
}

static int reqh_ctxs_are_valid(struct m0_mero *cctx)
{
	struct m0_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	const char                  *sname;
	int                          i;
	int                          rc = 0;

	M0_ENTRY();
	M0_PRE(cctx != NULL);

	if (cctx->cc_recv_queue_min_length < M0_NET_TM_RECV_QUEUE_DEF_LEN)
		cctx->cc_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN;

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		if (!reqh_ctx_args_are_valid(rctx)) {
			M0_LOG(M0_ERROR, "Missing or Invalid parameters");
			M0_RETURN(-EINVAL);
		}

		if (rctx->rc_recv_queue_min_length <
		    M0_NET_TM_RECV_QUEUE_DEF_LEN)
			rctx->rc_recv_queue_min_length =
				cctx->cc_recv_queue_min_length;

		if (rctx->rc_max_rpc_msg_size == 0)
			rctx->rc_max_rpc_msg_size = cctx->cc_max_rpc_msg_size;

		if (!stype_is_valid(rctx->rc_stype)) {
			M0_LOG(M0_ERROR, "storage_init_fail");
			cs_stob_types_list(cctx->cc_outfile);
			M0_RETURN(-EINVAL);
		}
		/*
		   Check if all the given end points in a reqh context are
		   valid.
		 */
		if (cs_eps_tlist_is_empty(&rctx->rc_eps)) {
			M0_LOG(M0_ERROR, "Endpoint missing");
			M0_RETURN(-EINVAL);
		}

		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_endpoint_validate(cctx, ep->ex_endpoint,
						  ep->ex_xprt);
			if (rc != 0) {
				M0_LOG(M0_ERROR, "endpoint_init_fail: %s: %d",
						 ep->ex_endpoint, rc);
				M0_RETURN(rc);
			}
		} m0_tl_endfor;

		/*
		   Check if the services are registered and are valid in a
		   reqh context.
		 */
		if (rctx->rc_nr_services == 0) {
			M0_LOG(M0_ERROR, "No Service specified");
			M0_RETURN(-EINVAL);
		}

		for (i = 0; i < rctx->rc_nr_services; ++i) {
			sname = rctx->rc_services[i];
			if (!m0_reqh_service_is_registered(sname)) {
				M0_LOG(M0_ERROR, "service_init_fail %s", sname);
				M0_RETURN(-ENOENT);
			}
			if (service_is_duplicate(rctx, sname)) {
				M0_LOG(M0_ERROR, "service_init_fail %s", sname);
				M0_RETURN(-EEXIST);
			}
		}
	} m0_tl_endfor;

	if (cctx->cc_pool_width <= 0) {
		M0_LOG(M0_ERROR, "Invalid pool width.\n"
				 "Use -w to provide a valid integer");
		M0_RETURN(-EINVAL);
	}
	M0_RETURN(rc);
}

/**
   Causes the process to run as a daemon if appropriate context flag is set.
   This involves forking, detaching from the keyboard if any, and ensuring
   SIGHUP will not affect the process.
   @note Must be called before any long-lived threads are created (i.e. at the
   time of calling, only the main thread should exist, although it is acceptable
   if threads are created and destroyed before going into daemon mode).  There
   is no Linux API to enforce this requirement.
   @note A trace log file opened before this function is called has a different
   process ID in the name than the process that continues to write to the file.
 */
static int cs_daemonize(struct m0_mero *cctx)
{
	int rc = 0;
	struct sigaction hup_act = {
		.sa_handler = SIG_IGN,
	};

	if (cctx->cc_daemon)
		rc = daemon(1 /* nochdir */, 0 /* redirect stdio */) ?:
		    sigaction(SIGHUP, &hup_act, NULL);
	return rc;
}

/**
   Parses a service string of the following forms:
   - service-type
   - service-type:uuid-str

   In the latter case it isolates and parses the UUID string, and returns it
   in the uuid parameter.

   @param str Input string
   @param svc Allocated service type name
   @param uuid Numerical UUID value if present and valid, or zero.
 */
static int service_string_parse(const char *str, char **svc,
				struct m0_uint128 *uuid)
{
	const char *colon;
	size_t      len;
	int         rc;

	uuid->u_lo = uuid->u_hi = 0;
	colon = strchr(str, ':');
	if (colon == NULL) {
		/** @todo replace with m0_strdup() when available */
		*svc = strdup(str);
		return *svc ? 0 : -ENOMEM;
	}

	/* isolate and copy the service type */
	len = colon - str;
	*svc = m0_alloc(len + 1);
	if (*svc == NULL)
		return -ENOMEM;
	strncpy(*svc, str, len);
	*(*svc + len) = '\0';

	/* parse the UUID */
	rc = m0_uuid_parse(++colon, uuid);
	if (rc != 0) {
		m0_free(*svc);
		*svc = NULL;
	}
	return rc;
}

/** Parses CLI arguments, filling m0_mero structure. */
static int _args_parse(struct m0_mero *cctx, int argc, char **argv,
		       const char **confd_addr, const char **profile,
		       const char **genders, bool *use_genders)
{
	int                     result;
	struct m0_reqh_context *rctx = NULL;
	int                     rc = 0;

	M0_ENTRY();
	M0_PRE(cctx != NULL);

	if (argc <= 1)
		M0_RETURN(-EINVAL);

#define _RETURN_EINVAL_UNLESS(rctx)   \
	do {                          \
		if (rctx == NULL) {   \
			rc = -EINVAL; \
			return;       \
		}                     \
	} while (0)

	result = M0_GETOPTS("m0d", argc, argv,
			/* -------------------------------------------
			 * Global options
			 */
			M0_VOIDARG('h', "m0d usage help",
				LAMBDA(void, (void)
				{
					cs_help(cctx->cc_outfile);
					rc = 1;
				})),
			M0_VOIDARG('x', "List supported transports",
				LAMBDA(void, (void)
				{
					cs_xprts_list(cctx->cc_outfile,
						      cctx->cc_xprts,
						      cctx->cc_xprts_nr);
					rc = 1;
				})),
			M0_VOIDARG('l', "List supported services",
				LAMBDA(void, (void)
				{
					printf("Supported services:\n");
					m0_reqh_service_list_print();
					rc = 1;
				})),
			M0_FORMATARG('Q', "Minimum TM Receive queue length",
				     "%i", &cctx->cc_recv_queue_min_length),
			M0_FORMATARG('M', "Maximum RPC message size", "%i",
				     &cctx->cc_max_rpc_msg_size),
			M0_STRINGARG('C', "confd endpoint address",
				LAMBDA(void, (const char *s)
				{
					M0_ASSERT(confd_addr != NULL);
					*confd_addr = s;
				})),
			M0_STRINGARG('P', "Configuration profile",
				LAMBDA(void, (const char *s)
				{
					M0_ASSERT(profile != NULL);
					*profile = s;
				})),
			M0_STRINGARG('G', "mdservice endpoint address",
				LAMBDA(void, (const char *s)
				{
					rc = ep_and_xprt_extract(&cctx->
								 cc_mds_epx, s);
				})),
			M0_STRINGARG('i', "ioservice endpoints list",
				LAMBDA(void, (const char *s)
				{
					rc = ep_and_xprt_append(
							&cctx->cc_ios_eps, s);
					M0_LOG(M0_DEBUG, "adding %s to ios "
							 "ep list %d", s, rc);
				})),
			M0_FORMATARG('w', "Pool Width", "%i",
				     &cctx->cc_pool_width),
			M0_FLAGARG('g', "Bootstrap from genders", use_genders),
			M0_STRINGARG('f', "Genders file",
				LAMBDA(void, (const char *s)
				{
					M0_ASSERT(genders != NULL);
					*genders = s;
				})),
			M0_FLAGARG('Z', "Run as a daemon", &cctx->cc_daemon),

			/* -------------------------------------------
			 * Request handler options
			 */
			M0_VOIDARG('r', "Start request handler",
				LAMBDA(void, (void)
				{
					rctx = NULL;
					rctx = cs_reqh_ctx_alloc(cctx);
					if (rctx == NULL) {
						rc = -ENOMEM;
						return;
					}
					rctx->rc_nr_services = 0;
				})),
			M0_VOIDARG('p', "Prepare storage (root session,"
				   " hierarchy root, etc)",
				LAMBDA(void, (void)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_prepare_storage = 1;
				})),
			M0_STRINGARG('D', "Database environment path",
				LAMBDA(void, (const char *s)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_dbpath = s;
				})),
			M0_STRINGARG('c', "Path to the configuration database",
				LAMBDA(void, (const char *s)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_confdb = s;
				})),
			M0_STRINGARG('T', "Storage domain type",
				LAMBDA(void, (const char *s)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_stype = s;
				})),
			M0_STRINGARG('A', "ADDB Storage domain name",
				LAMBDA(void, (const char *s)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_addb_stpath = s;
				})),
			M0_STRINGARG('S', "Storage domain name",
				LAMBDA(void, (const char *s)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_stpath = s;
				})),
			M0_STRINGARG('d', "device configuration file",
				LAMBDA(void, (const char *s)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_dfilepath = s;
				})),
			M0_NUMBERARG('q', "Minimum TM recv queue length",
				LAMBDA(void, (int64_t length)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_recv_queue_min_length = length;
				})),
			M0_NUMBERARG('m', "Maximum RPC message size",
				LAMBDA(void, (int64_t size)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_max_rpc_msg_size = size;
				})),
			/*
			 * XXX TODO Test the following use case: endpoints are
			 * specified both via `-e' CLI option and via
			 * configuration.
			 */
			M0_STRINGARG('e', "Network endpoint,"
				     " e.g. transport:address",
				LAMBDA(void, (const char *s)
				{
				      _RETURN_EINVAL_UNLESS(rctx);
				      rc = ep_and_xprt_append(&rctx->rc_eps, s);
				})),
			M0_STRINGARG('s', "Services to be configured",
				LAMBDA(void, (const char *s)
				{
					int i;
					_RETURN_EINVAL_UNLESS(rctx);
					if (rctx->rc_nr_services >=
					    rctx->rc_max_services) {
						rc = -E2BIG;
						M0_LOG(M0_ERROR,
						       "Too many services");
						return;
					}
					i = rctx->rc_nr_services;
					rc = service_string_parse(s,
						   &rctx->rc_services[i],
						   &rctx->rc_service_uuids[i]);
					if (rc == 0)
						M0_CNT_INC(
							  rctx->rc_nr_services);
				})),
			M0_VOIDARG('v', "Print version and exit",
				LAMBDA(void, (void)
				{
					m0_build_info_print();
					rc = 1;
				})),
			);
#undef _RETURN_EINVAL_UNLESS

	M0_RETURN(result ?: rc);
}

static int cs_args_parse(struct m0_mero *cctx, int argc, char **argv)
{
	int         rc;
	const char *confd_addr = NULL;
	const char *profile = NULL;
	const char *genders = NULL;
	bool        use_genders = false;

	M0_ENTRY();

	rc = _args_parse(cctx, argc, argv, &confd_addr, &profile,
			 &genders, &use_genders);
	if (rc != 0)
		M0_RETURN(rc);

	if (genders != NULL && !use_genders)
		M0_RETERR(-EPROTO, "-f genders file specified without -g");
	/**
	 * @todo allow bootstrap via genders and confd afterward, but currently
	 * confd is only used for bootstrap, thus a conflict if both present.
	 */
	if (use_genders && profile != NULL)
		M0_RETERR(-EPROTO, "genders use conflicts with confd profile");
	if (use_genders) {
		struct cs_args *args = &cctx->cc_args;
		bool global_daemonize = cctx->cc_daemon;

		rc = cs_genders_to_args(args, argv[0], genders);
		if (rc != 0)
			M0_RETURN(rc);

		rc = _args_parse(cctx, args->ca_argc, args->ca_argv,
				 NULL, NULL, NULL, &use_genders);
		cctx->cc_daemon |= global_daemonize;
	}
	if ((confd_addr == NULL) != (profile == NULL))
		M0_RETERR(-EPROTO, "%s is not specified",
			  (char *)(profile == NULL ? "configuration profile" :
				   "confd address"));
	if (confd_addr != NULL) {
		struct cs_args *args = &cctx->cc_args;

		rc = cs_conf_to_args(args, confd_addr, profile);
		if (rc != 0)
			M0_RETURN(rc);

		rc = _args_parse(cctx, args->ca_argc, args->ca_argv,
				 NULL, NULL, NULL, &use_genders);
	}
	if (rc == 0 && use_genders)
		rc = -EINVAL;
	M0_RETURN(rc);
}

int m0_cs_setup_env(struct m0_mero *cctx, int argc, char **argv)
{
	int rc;

	M0_PRE(cctx != NULL);

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	m0_rwlock_write_lock(&cctx->cc_rwlock);
	rc = cs_args_parse(cctx, argc, argv) ?:
		reqh_ctxs_are_valid(cctx) ?:
		cs_daemonize(cctx) ?:
		cs_net_domains_init(cctx) ?:
		cs_buffer_pool_setup(cctx) ?:
		cs_request_handlers_start(cctx) ?:
		cs_rpc_machines_init(cctx);
	m0_rwlock_write_unlock(&cctx->cc_rwlock);

	if (rc < 0) {
		M0_LOG(M0_ERROR, "m0_cs_setup_env: %d", rc);
		cs_usage(cctx->cc_outfile);
	}

	return rc;
}

int m0_cs_start(struct m0_mero *cctx)
{
	M0_ENTRY();
	M0_PRE(cctx != NULL);
	M0_RETURN(cs_services_init(cctx));
}

int m0_cs_init(struct m0_mero *cctx, struct m0_net_xprt **xprts,
	       size_t xprts_nr, FILE * out)
{
	M0_PRE(cctx != NULL && xprts != NULL && xprts_nr > 0 && out != NULL);

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	cctx->cc_xprts = xprts;
	cctx->cc_xprts_nr = xprts_nr;
	cctx->cc_outfile = out;
	cs_mero_init(cctx);

	return 0;
}

void m0_cs_fini(struct m0_mero *cctx)
{
	M0_PRE(cctx != NULL);

	M0_ENTRY();

	cs_request_handlers_stop(cctx);
	cs_buffer_pool_fini(cctx);
	cs_net_domains_fini(cctx);
	cs_mero_fini(cctx);
}

/** @} endgroup m0d */

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
