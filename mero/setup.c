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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_OTHER
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/getopts.h"
#include "lib/misc.h"
#include "lib/finject.h"    /* M0_FI_ENABLED */

#include "balloc/balloc.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "reqh/reqh.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h"
#include "mero/setup.h"
#include "mero/setup_internal.h"
#include "mero/magic.h"
#include "rpc/rpclib.h"
#include "rpc/rpc_internal.h"
/**
   @addtogroup m0d
   @{
 */

static const struct m0_addb_loc cs_addb_loc = {
	.al_name = "Mero setup"
};

static const struct m0_addb_ctx_type cs_addb_ctx_type = {
	.act_name = "Mero setup"
};

M0_ADDB_EV_DEFINE(arg_fail, "argument_failure",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(setup_fail, "setup_failure",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(storage_init_fail, "storage_init_failure",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(endpoint_init_fail, "endpoint_init_failure",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(service_init_fail, "service_init_failure",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(rpc_init_fail, "rpc_init_failure",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);
M0_ADDB_EV_DEFINE(reqh_init_fail, "reqh_int_failure",
		  M0_ADDB_EVENT_FUNC_FAIL, M0_ADDB_FUNC_CALL);

/**
   Represents cob domain id, it is incremented for every new cob domain.

   @todo Have a generic mechanism to generate unique cob domain id.
 */
static int cdom_id;

M0_TL_DESCR_DEFINE(cs_buffer_pools, "buffer pools in the mero context",
		   static, struct cs_buffer_pool, cs_bp_linkage, cs_bp_magic,
		   M0_CS_BUFFER_POOL_MAGIC, M0_CS_BUFFER_POOL_HEAD_MAGIC);
M0_TL_DEFINE(cs_buffer_pools, static, struct cs_buffer_pool);

M0_TL_DESCR_DEFINE(cs_eps, "cs endpoints", static, struct cs_endpoint_and_xprt,
		   ex_linkage, ex_magix, M0_CS_ENDPOINT_AND_XPRT_MAGIC,
		   M0_CS_EPS_HEAD_MAGIC);

M0_TL_DEFINE(cs_eps, static, struct cs_endpoint_and_xprt);

static struct m0_bob_type cs_eps_bob;
M0_BOB_DEFINE(static, &cs_eps_bob, cs_endpoint_and_xprt);

/**
   Currently supported stob types in mero context.
 */
static const char *cs_stypes[] = {
	[LINUX_STOB] = "Linux",
	[AD_STOB]    = "AD"
};

M0_TL_DESCR_DEFINE(rhctx, "reqh contexts", static, struct cs_reqh_context,
		   rc_linkage, rc_magix, M0_CS_REQH_CTX_MAGIC,
		   M0_CS_REQH_CTX_HEAD_MAGIC);

M0_TL_DEFINE(rhctx, static, struct cs_reqh_context);

static struct m0_bob_type rhctx_bob;
M0_BOB_DEFINE(static, &rhctx_bob, cs_reqh_context);

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
static void cs_xprts_list(FILE *out, struct m0_net_xprt **xprts, size_t xprts_nr)
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
	for (i = 0; i < STOBS_NR; ++i)
		fprintf(out, " %s\n", cs_stypes[i]);
}

/**
   Checks if the specified storage type is supported in a mero context.

   @param stype Storage type

   @pre stype != NULL
 */
static bool stype_is_valid(const char *stype)
{
	M0_PRE(stype != NULL);

	return  strcasecmp(stype, cs_stypes[AD_STOB]) == 0 ||
		strcasecmp(stype, cs_stypes[LINUX_STOB]) == 0;
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
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep_xprt;

	M0_PRE(cctx != NULL && xprt != NULL && ep != NULL);

	cnt = 0;
	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(cs_reqh_context_invariant(rctx));
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
	M0_PRE(cctx != NULL && ep != NULL && xprt_name != NULL);

	xprt = cs_xprt_lookup(xprt_name, cctx->cc_xprts, cctx->cc_xprts_nr);
	if (xprt == NULL)
		M0_RETURN(-EINVAL);

	M0_RETURN(cs_endpoint_is_duplicate(cctx, xprt, ep) ? -EADDRINUSE : 0);
}

/**
   Extracts network transport name and network endpoint address from given
   mero endpoint.
   Mero endpoint is of 2 parts network xprt:network endpoint.
 */
static int ep_and_xprt_append(struct cs_reqh_context *rctx, const char *ep)
{
	char                        *sptr;
	struct cs_endpoint_and_xprt *epx;
	char                        *endpoint;
	struct m0_addb_ctx          *addb;
	int ep_len = min32u(strlen(ep) + 1, CS_MAX_EP_ADDR_LEN);

	M0_PRE(ep != NULL);

	addb = &rctx->rc_mero->cc_addb;
	M0_ALLOC_PTR_ADDB(epx, addb, &cs_addb_loc);
	if (epx == NULL)
		return -ENOMEM;
	epx->ex_cep = ep;

	M0_ALLOC_ARR_ADDB(epx->ex_scrbuf, ep_len, addb, &cs_addb_loc);
	if (epx->ex_scrbuf == NULL) {
		m0_free(epx);
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
	cs_eps_tlink_init_at_tail(epx, &rctx->rc_eps);
	return 0;
err:
	m0_free(epx->ex_scrbuf);
	m0_free(epx);
	return -EINVAL;
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

	M0_PRE(rctx != NULL);

	M0_ASSERT(cs_reqh_context_invariant(rctx));
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
   mero context.

   @param cctx Mero context

   @see m0_mero
 */
static struct cs_reqh_context *cs_reqh_ctx_alloc(struct m0_mero *cctx)
{
	struct cs_reqh_context *rctx;

	M0_PRE(cctx != NULL);

	M0_ALLOC_PTR_ADDB(rctx, &cctx->cc_addb, &cs_addb_loc);
	if (rctx == NULL)
		return NULL;

	rctx->rc_max_services = m0_reqh_service_types_length();
	if (rctx->rc_max_services == 0) {
		M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc, service_init_fail,
			    "No services available", -ENOENT);
		goto err;
	}

	M0_ALLOC_ARR_ADDB(rctx->rc_services, rctx->rc_max_services,
			  &cctx->cc_addb, &cs_addb_loc);
	if (rctx->rc_services == NULL)
		goto err;

	cs_reqh_context_bob_init(rctx);
	cs_eps_tlist_init(&rctx->rc_eps);
	rhctx_tlink_init_at_tail(rctx, &cctx->cc_reqh_ctxs);
	rctx->rc_mero = cctx;

	return rctx;
err:
	m0_free(rctx);
	return NULL;
}

static void cs_reqh_ctx_free(struct cs_reqh_context *rctx)
{
	struct cs_endpoint_and_xprt *ep;

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
	m0_free(rctx->rc_services);
	rhctx_tlink_del_fini(rctx);
	cs_reqh_context_bob_fini(rctx);
	m0_free(rctx);
}

/**
   Finds network domain for specified network transport in a given mero
   context.

   @param cctx Mero context
   @param xprt_name Type of network transport to be initialised

   @pre cctx != NULL && xprt_name != NULL

   @see m0_cs_init()
 */
M0_INTERNAL struct m0_net_domain *m0_cs_net_domain_locate(struct m0_mero
							  *cctx,
							  const char *xprt_name)
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

static struct m0_net_buffer_pool *cs_buffer_pool_get(struct m0_mero *cctx,
						     struct m0_net_domain *ndom)
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

	M0_ALLOC_PTR_ADDB(rpcmach, &cctx->cc_addb, &cs_addb_loc);
	if (rpcmach == NULL)
		return -ENOMEM;

	if (max_rpc_msg_size > m0_net_domain_get_max_buffer_size(ndom)) {
		m0_free(rpcmach);
		return -EINVAL;
	}

	buffer_pool = cs_buffer_pool_get(cctx, ndom);
	rc = m0_rpc_machine_init(rpcmach, &reqh->rh_mdstore->md_dom, ndom, ep,
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
	FILE                        *ofd;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;

	M0_PRE(cctx != NULL);

	ofd = cctx->cc_outfile;
	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(cs_reqh_context_invariant(rctx));
		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_rpc_machine_init(cctx, ep->ex_xprt,
						 ep->ex_endpoint,
						 ep->ex_tm_colour,
						 rctx->rc_recv_queue_min_length,
						 rctx->rc_max_rpc_msg_size,
						 &rctx->rc_reqh);
			if (rc != 0) {
				M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    rpc_init_fail,
					    ep->ex_cep, rc);
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
	struct cs_reqh_context      *rctx;
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
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	uint32_t                     min_queue_len_total = 0;

	M0_PRE(cctx != NULL);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(cs_reqh_context_bob_check(rctx));
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

		bufs_nr  = m0_rpc_bufs_nr(max_recv_queue_len, tms_nr);

		M0_ALLOC_PTR(cs_bp);
		if (cs_bp == NULL) {
			rc = -ENOMEM;
			break;
		}

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

static int cs_stob_file_load(const char *dfile, struct cs_stobs *stob,
			     struct m0_addb_ctx *addb)
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
			     struct m0_dbenv *db, const char *f_path,
			     struct m0_addb_ctx *addb)
{
	int                 rc;
	char                ad_dname[MAXPATHLEN];
	struct m0_stob_id  *bstob_id;
	struct m0_stob    **bstob;
	struct m0_balloc   *cb;
	struct cs_ad_stob  *adstob;
	struct m0_dtx      *tx;

	M0_ALLOC_PTR_ADDB(adstob, addb, &cs_addb_loc);
	if (adstob == NULL)
		return -ENOMEM;

	tx = &stob->s_tx;
	bstob = &adstob->as_stob_back;
	bstob_id = &adstob->as_id_back;
	bstob_id->si_bits.u_hi = cid;
	bstob_id->si_bits.u_lo = 0xadf11e;
	rc = m0_stob_find(stob->s_ldom, bstob_id, bstob);
	if (rc == 0) {
		if (f_path != NULL)
			rc = m0_linux_stob_link(stob->s_ldom, *bstob, f_path,
						tx);
		if (rc == 0 || rc == -EEXIST)
			rc = m0_stob_create_helper(stob->s_ldom, tx, bstob_id,
						   bstob);
			if (rc == 0)
				m0_stob_put(*bstob);
	}

	if (rc == 0 && M0_FI_ENABLED("ad_domain_locate_fail"))
		rc = -EINVAL;

	if (rc == 0) {
		sprintf(ad_dname, "%lx%lx", bstob_id->si_bits.u_hi,
			bstob_id->si_bits.u_lo);
		rc = m0_stob_domain_locate(&m0_ad_stob_type, ad_dname,
					   &adstob->as_dom);
	}

	if (rc != 0) {
		m0_stob_put(*bstob);
		m0_free(adstob);
	}

	if (rc == 0) {
		cs_ad_stob_bob_init(adstob);
		astob_tlink_init_at_tail(adstob, &stob->s_adoms);
	}

	if (rc == 0)
		rc = m0_balloc_allocate(cid, &cb);
	if (rc == 0)
		rc = m0_ad_stob_setup(adstob->as_dom, db,
				      *bstob, &cb->cb_ballroom,
				      BALLOC_DEF_CONTAINER_SIZE,
				      BALLOC_DEF_BLOCK_SHIFT,
				      BALLOC_DEF_BLOCKS_PER_GROUP,
				      BALLOC_DEF_RESERVED_GROUPS);

	if (rc == 0 && M0_FI_ENABLED("ad_stob_setup_fail"))
		rc = -EINVAL;

	return rc;
}

/**
   Initialises AD type stob.
 */
static int cs_ad_stob_init(struct cs_stobs *stob, struct m0_dbenv *db,
			   struct m0_addb_ctx *addb)
{
	int                rc;
	int                result;
	uint64_t           cid;
	const char        *f_path;
	yaml_document_t   *doc;
	yaml_node_t       *node;
	yaml_node_t       *s_node;
	yaml_node_item_t  *item;

	M0_PRE(stob != NULL);

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
	struct m0_stob_domain *sdom;

	rc = m0_stob_domain_locate(&m0_linux_stob_type, stob_path,
				   &stob->s_ldom);
	if (rc == 0) {
		sdom = stob->s_ldom;
		rc = m0_linux_stob_setup(sdom, false);
	}

	return rc;
}

static void cs_ad_stob_fini(struct cs_stobs *stob)
{
	struct m0_stob        *bstob;
	struct cs_ad_stob     *adstob;
	struct m0_stob_domain *adom;

	M0_PRE(stob != NULL);

	m0_tl_for(astob, &stob->s_adoms, adstob) {
		M0_ASSERT(cs_ad_stob_bob_check(adstob) &&
			  adstob->as_dom != NULL);
		bstob = adstob->as_stob_back;
		adom = adstob->as_dom;
		if (bstob != NULL && bstob->so_state == CSS_EXISTS)
			m0_stob_put(bstob);
		adom->sd_ops->sdo_fini(adom);
		astob_tlink_del_fini(adstob);
		cs_ad_stob_bob_fini(adstob);
		m0_free(adstob);
	} m0_tl_endfor;
	astob_tlist_fini(&stob->s_adoms);
}

static void cs_linux_stob_fini(struct cs_stobs *stob)
{
	M0_PRE(stob != NULL);

	if (stob->s_ldom != NULL)
		stob->s_ldom->sd_ops->sdo_fini(stob->s_ldom);
}

M0_INTERNAL struct m0_stob_domain *m0_cs_stob_domain_find(struct m0_reqh *reqh,
							  const struct
							  m0_stob_id *stob_id)
{
	struct cs_reqh_context  *rqctx;
	struct cs_stobs         *stob;
	struct cs_ad_stob       *adstob;

	rqctx = bob_of(reqh, struct cs_reqh_context, rc_reqh, &rhctx_bob);
	stob = &rqctx->rc_stob;

	if (strcasecmp(stob->s_stype, cs_stypes[LINUX_STOB]) == 0)
		return stob->s_ldom;
	else if (strcasecmp(stob->s_stype, cs_stypes[AD_STOB]) == 0) {
		m0_tl_for(astob, &stob->s_adoms, adstob) {
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
			   struct cs_stobs *stob, struct m0_dbenv *db,
			   struct m0_addb_ctx *addb)
{
	int               rc;
	int               slen;
	struct m0_dtx    *tx;
	char             *objpath;
	static const char objdir[] = "/o";

	M0_PRE(stob_type != NULL && stob_path != NULL && stob != NULL);

	stob->s_stype = stob_type;

	slen = strlen(stob_path);
	M0_ALLOC_ARR_ADDB(objpath, slen + ARRAY_SIZE(objdir), addb,
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
	m0_dtx_init(tx);
	rc = m0_dtx_open(tx, db);
	if (rc != 0) {
		m0_dtx_done(tx);
		goto out;
	}
	rc = cs_linux_stob_init(stob_path, stob);
	if (rc != 0)
		goto out;

	if (strcasecmp(stob_type, cs_stypes[AD_STOB]) == 0)
		rc = cs_ad_stob_init(stob, db, addb);

out:
	m0_free(objpath);

	return rc;
}

/**
   Finalises storage for a request handler in a mero context.
 */
static void cs_storage_fini(struct cs_stobs *stob)
{
	M0_PRE(stob != NULL);

	m0_dtx_done(&stob->s_tx);
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

   @post m0_reqh_service_invariant(service)
 */
static int cs_service_init(const char *service_name, struct m0_reqh *reqh)
{
	int                          rc;
	struct m0_reqh_service_type *stype;
	struct m0_reqh_service      *service;

	M0_PRE(service_name != NULL && reqh != NULL);

	stype = m0_reqh_service_type_find(service_name);
	if (stype == NULL)
		return -EINVAL;

	rc = m0_reqh_service_allocate(stype, &service);
	if (rc == 0) {
		m0_reqh_service_init(service, reqh);
		rc = m0_reqh_service_start(service);
		if (rc != 0)
			m0_reqh_service_fini(service);
	}

	return rc;
}

/**
   Initialises set of services specified in a request handler context.
   Services are started once the mero context is configured successfully
   which includes network domains, request handlers, and rpc machines.

   @param cctx Mero context
 */
static int cs_services_init(struct m0_mero *cctx)
{
	int                     idx;
	int                     rc = 0;
	struct cs_reqh_context *rctx;

	M0_PRE(cctx != NULL);

	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(cs_reqh_context_invariant(rctx));
		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			rc = cs_service_init(rctx->rc_services[idx],
					     &rctx->rc_reqh);
			if (rc != 0)
				return rc;
		}
	} m0_tl_endfor;

	return rc;
}

/**
   Finalises a service.
   Transitions service to M0_RSPH_STOPPING phase, stops a service and then
   finalises the same.

   @param service Service to be finalised

   @pre service != NULL

   @see m0_reqh_service_stop()
   @see m0_reqh_service_fini()
 */
static void cs_service_fini(struct m0_reqh_service *service)
{
	M0_PRE(service != NULL);

	m0_reqh_service_stop(service);
	m0_reqh_service_fini(service);
}

/**
   Finalises all the services registered with a request handler.
   Also traverses through the services list and invokes cs_service_fini() on
   each individual service.

   @param reqh Request handler of which the services are to be finalised

   @pre reqh != NULL
 */
static void cs_services_fini(struct m0_reqh *reqh)
{
	struct m0_reqh_service *service;
	struct m0_tl           *services;

	M0_PRE(reqh != NULL);

	services = &reqh->rh_services;
	m0_tl_for(m0_reqh_svc, services, service) {
		M0_ASSERT(m0_reqh_service_invariant(service));
		cs_service_fini(service);
	} m0_tl_endfor;
}

/**
   Initialises network domains per given distinct xport:endpoint pair in a
   mero context.

   @param cctx Mero context
 */
static int cs_net_domains_init(struct m0_mero *cctx)
{
	int                          rc = 0;
	size_t                       xprts_nr;
	FILE                        *ofd;
	struct m0_net_xprt         **xprts;
	struct m0_net_xprt          *xprt;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	struct m0_net_domain        *ndom;

	M0_PRE(cctx != NULL);

	xprts = cctx->cc_xprts;
	xprts_nr = cctx->cc_xprts_nr;

	ofd = cctx->cc_outfile;
	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(cs_reqh_context_invariant(rctx));
		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));

			xprt = cs_xprt_lookup(ep->ex_xprt, xprts, xprts_nr);
			if (xprt == NULL) {
				M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    endpoint_init_fail,
					    ep->ex_xprt, -EINVAL);
				return -EINVAL;
			}

			ndom = m0_cs_net_domain_locate(cctx, ep->ex_xprt);
			if (ndom != NULL)
				continue;

			rc = m0_net_xprt_init(xprt);
			if (rc != 0)
				return rc;

			M0_ALLOC_PTR_ADDB(ndom, &cctx->cc_addb, &cs_addb_loc);
			if (ndom == NULL) {
				m0_net_xprt_fini(xprt);
				return -ENOMEM;
			}
			rc = m0_net_domain_init(ndom, xprt);
			if (rc != 0) {
				m0_free(ndom);
				m0_net_xprt_fini(xprt);
				return rc;
			}
			m0_net_domain_bob_init(ndom);
			ndom_tlink_init_at_tail(ndom, &cctx->cc_ndoms);
		} m0_tl_endfor;
	} m0_tl_endfor;

	return rc;
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

static int cs_storage_prepare(struct cs_reqh_context *rctx)
{
	struct m0_db_tx tx;
	int rc;

	rc = m0_db_tx_init(&tx, &rctx->rc_db, 0);
	if (rc == 0) {
		rc = m0_rpc_root_session_cob_create(&rctx->rc_mdstore.md_dom, &tx);
		if (rc == 0) {
			m0_db_tx_commit(&tx);
		} else {
			m0_db_tx_abort(&tx);
		}
	}
	return rc;
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
static int cs_request_handler_start(struct cs_reqh_context *rctx)
{
	int                      rc;
	struct m0_addb_ctx      *addb;

	addb = &rctx->rc_mero->cc_addb;
	rc = m0_dbenv_init(&rctx->rc_db, rctx->rc_dbpath, 0);
	if (rc != 0) {
		M0_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "m0_dbenv_init", rc);
		return rc;
	}
	if (rctx->rc_dfilepath != NULL) {
		rc = cs_stob_file_load(rctx->rc_dfilepath, &rctx->rc_stob,
				       addb);
		if (rc != 0) {
			M0_ADDB_ADD(addb, &cs_addb_loc,
				    storage_init_fail,
				    "Failed to load device configuration file",
				    rc);
			return rc;
		}
	}

	if (rc == 0)
		rc = cs_storage_init(rctx->rc_stype, rctx->rc_stpath,
				     &rctx->rc_stob, &rctx->rc_db, addb);
	if (rc != 0) {
		M0_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "cs_storage_init", rc);
		goto cleanup_stob;
	}

	rctx->rc_cdom_id.id = ++cdom_id;

	/** Mkfs cob domain before using it. */
	if (rctx->rc_prepare_storage) {
		/*
		 * Init mdstore without root cob init first. Now we can use its
		 * cob domain for mkfs.
		 */
		rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id,
				     &rctx->rc_db, false);
		if (rc != 0) {
			M0_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
				    "m0_mdstore_init", rc);
			goto cleanup_stob;
		}
		rc = cs_storage_prepare(rctx);
		if (rc != 0) {
			M0_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
				    "cs_storage_prepare", rc);
			goto cleanup_mdstore;
		}
		m0_mdstore_fini(&rctx->rc_mdstore);
	}

	rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id, &rctx->rc_db,
			     true);
	if (rc != 0) {
		M0_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "m0_cob_domain_init", rc);
		goto cleanup_stob;
	}
	rc = m0_fol_init(&rctx->rc_fol, &rctx->rc_db);
	if (rc != 0) {
		M0_ADDB_ADD(addb, &cs_addb_loc, reqh_init_fail,
			    "m0_fol_init", rc);
		goto cleanup_mdstore;
	}
	rc = m0_reqh_init(&rctx->rc_reqh, NULL, &rctx->rc_db, &rctx->rc_mdstore,
			  &rctx->rc_fol, NULL);
	if (rc == 0) {
		rctx->rc_state = RC_INITIALISED;
		return 0;
	}

	m0_fol_fini(&rctx->rc_fol);
cleanup_mdstore:
	m0_mdstore_fini(&rctx->rc_mdstore);
cleanup_stob:
	cs_storage_fini(&rctx->rc_stob);
	m0_dbenv_fini(&rctx->rc_db);
	M0_ASSERT(rc != 0);
	return rc;
}

/**
   Configures one or more request handler contexts and starts corresponding
   request handlers in each context.
 */
static int cs_request_handlers_start(struct m0_mero *cctx)
{
	int                     rc = 0;
	struct cs_reqh_context *rctx;
	FILE                   *ofd;

	M0_PRE(cctx != NULL);

	ofd = cctx->cc_outfile;
	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(cs_reqh_context_invariant(rctx));
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

   @pre cs_reqh_context_invariant()
 */
static void cs_request_handler_stop(struct cs_reqh_context *rctx)
{
	struct m0_reqh *reqh;

	M0_PRE(cs_reqh_context_invariant(rctx));

	reqh = &rctx->rc_reqh;
	m0_reqh_shutdown_wait(reqh);

	cs_services_fini(reqh);
	cs_rpc_machines_fini(reqh);
	m0_reqh_fini(reqh);
	m0_fol_fini(&rctx->rc_fol);
	m0_mdstore_fini(&rctx->rc_mdstore);
	cs_storage_fini(&rctx->rc_stob);
	m0_dbenv_fini(&rctx->rc_db);
}

/**
   Finalises all the request handler contexts within a mero context.

   @param cctx Mero context to which the reqeust handler contexts belong
 */
static void cs_request_handlers_stop(struct m0_mero *cctx)
{
	struct cs_reqh_context *rctx;

	M0_PRE(cctx != NULL);

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
	int                      idx;
	struct cs_reqh_context  *rctx;
	struct m0_reqh		*reqh;

	M0_PRE(cctx != NULL);
	M0_PRE(service_name != NULL);

	m0_rwlock_read_lock(&cctx->cc_rwlock);
	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		M0_ASSERT(cs_reqh_context_invariant(rctx));

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			if (strcmp(rctx->rc_services[idx],
				   service_name) == 0) {
				reqh = &rctx->rc_reqh;
				m0_rwlock_read_unlock(&cctx->cc_rwlock);
				return reqh;
			}
		}
	} m0_tl_endfor;
	m0_rwlock_read_unlock(&cctx->cc_rwlock);

	return NULL;

}
M0_EXPORTED(m0_cs_reqh_get);

static struct cs_reqh_context *cs_reqh_ctx_get(struct m0_reqh *reqh)
{
	struct cs_reqh_context *rqctx;

	M0_PRE(m0_reqh_invariant(reqh));

	rqctx = bob_of(reqh, struct cs_reqh_context, rc_reqh, &rhctx_bob);
	M0_POST(cs_reqh_context_invariant(rqctx));

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

	m0_addb_ctx_init(&cctx->cc_addb, &cs_addb_ctx_type,
			 &m0_addb_global_ctx);
}

/**
   Finalises a mero context.

   @pre cctx != NULL

   @param cctx Mero context to be finalised
 */
static void cs_mero_fini(struct m0_mero *cctx)
{
	M0_PRE(cctx != NULL);

	rhctx_tlist_fini(&cctx->cc_reqh_ctxs);
	cs_buffer_pools_tlist_fini(&cctx->cc_buffer_pools);
	ndom_tlist_fini(&cctx->cc_ndoms);
	m0_rwlock_fini(&cctx->cc_rwlock);
	m0_addb_ctx_fini(&cctx->cc_addb);
}

/**
   Displays usage of m0d program.

   @param out File to which the output is written
 */
static void cs_usage(FILE *out)
{
	M0_PRE(out != NULL);

	fprintf(out, "Usage: m0d [-h] [-x] [-l]\n"
		   "    or m0d GlobalFlags ReqHSpec+\n"
		   "       where\n"
		   "         GlobalFlags := [-M RPCMaxMessageSize]"
		   " [-Q MinReceiveQueueLength]\n"
		   "         ReqHspec    := -r -T StobType -DDBPath"
		   " -SStobFile [-dDevfile] {-e xport:endpoint}+\n"
		   "                        {-s service}+"
		   " [-q MinReceiveQueueLength] [-m RPCMaxMessageSize]\n");
}

/**
   Displays help for m0d program.

   @param out File to which the output is written
 */
static void cs_help(FILE *out)
{
	M0_PRE(out != NULL);

	cs_usage(out);
	fprintf(out, "Every -r option represents a request handler set.\n"
		   "All the parameters in a request handler set are "
		   "mandatory.\nThere can be "
		   "multiple such request handler sets in a single mero "
		   "process.\n"
		   "Endpoints and services can be specified multiple times "
		   "using -e and -s options\nin a request handler set.\n"
		   "-h Prints mero usage help.\n"
		   "   e.g. m0d -h\n"
		   "-x Lists supported network transports.\n"
		   "   e.g. m0d -x\n"
		   "-l Lists supported services on this node.\n"
		   "   e.g. m0d -l\n"
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
/* Currently m0d.c does not pick up the in-mem transport. There is no
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
		   "   e.g. ./m0d -Q 4 -M 4096 -r -T linux\n"
		   "        -D dbpath -S stobfile -e lnet:172.18.50.40@o2ib1:12345:34:1 \n"
		   "	    -s mds -q 8 -m 65536 \n");
}

static int reqh_ctxs_are_valid(struct m0_mero *cctx)
{
	int                          rc = 0;
	int                          idx;
	FILE                        *ofd;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
	const char                  *sname;

	M0_ENTRY();
	M0_PRE(cctx != NULL);

	if (cctx->cc_recv_queue_min_length < M0_NET_TM_RECV_QUEUE_DEF_LEN)
		cctx->cc_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN;

	ofd = cctx->cc_outfile;
	m0_tl_for(rhctx, &cctx->cc_reqh_ctxs, rctx) {
		if (!reqh_ctx_args_are_valid(rctx)){
			M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc, arg_fail,
				    "Missing or Invalid parameters", -EINVAL);
			M0_RETURN(-EINVAL);
		}

		if (rctx->rc_recv_queue_min_length <
		    M0_NET_TM_RECV_QUEUE_DEF_LEN)
			rctx->rc_recv_queue_min_length =
				cctx->cc_recv_queue_min_length;

		if (rctx->rc_max_rpc_msg_size == 0)
			rctx->rc_max_rpc_msg_size =
				cctx->cc_max_rpc_msg_size;

		if (!stype_is_valid(rctx->rc_stype)) {
			M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
				    storage_init_fail,
				    rctx->rc_stype, -EINVAL);
			cs_stob_types_list(ofd);
			M0_RETURN(-EINVAL);
		}
		/*
		   Check if all the given end points in a reqh context are
		   valid.
		 */
		if (cs_eps_tlist_is_empty(&rctx->rc_eps)) {
			M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc, arg_fail,
				    "Endpoint is missing", -EINVAL);
			M0_RETURN(-EINVAL);
		}

		m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
			M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_endpoint_validate(cctx, ep->ex_endpoint,
						  ep->ex_xprt);
			if (rc != 0) {
				M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    endpoint_init_fail,
					    ep->ex_cep, rc);
				M0_RETURN(rc);
			}
		} m0_tl_endfor;

		/*
		   Check if the services are registered and are valid in a
		   reqh context.
		 */
		if (rctx->rc_snr == 0) {
			M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
				    service_init_fail,
				    "No Service specified", -EINVAL);
			M0_RETURN(-EINVAL);
		}

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			sname = rctx->rc_services[idx];
			if (!m0_reqh_service_is_registered(sname)) {
				M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    service_init_fail,
					    sname, -ENOENT);
				M0_RETURN(-ENOENT);
			}
			if (service_is_duplicate(rctx, sname)) {
				M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
					    service_init_fail,
					    sname, -EEXIST);
				M0_RETURN(-EEXIST);
			}
		}
	} m0_tl_endfor;

	M0_RETURN(rc);
}

/**
   Parses given arguments and allocates request handler context, if all the
   required arguments are provided and valid.
   Every allocated request handler context is added to the list of the same
   in given mero context.
 */
static int cs_parse_args(struct m0_mero *cctx, int argc, char **argv)
{
	int                     result;
	struct cs_reqh_context *rctx = NULL;
	int                     rc = 0;

	M0_PRE(cctx != NULL);

	if (argc <= 1)
		return -EINVAL;

#define _RETURN_EINVAL_UNLESS(rctx)  \
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
			M0_VOIDARG('x', "List Supported transports",
				LAMBDA(void, (void)
				{
					cs_xprts_list(cctx->cc_outfile,
						      cctx->cc_xprts,
						      cctx->cc_xprts_nr);
					rc = 1;
				})),
			M0_VOIDARG('l', "List Supported services",
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
					rctx->rc_snr = 0;
				})),
			M0_VOIDARG('p', "Prepare storage (root session,"
				   " hierarchy root, etc)",
				LAMBDA(void, (void)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_prepare_storage = 1;
				})),
			M0_STRINGARG('D', "Database environment path",
				LAMBDA(void, (const char *str)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_dbpath = str;
				})),
			M0_STRINGARG('T', "Storage domain type",
				LAMBDA(void, (const char *str)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_stype = str;
				})),
			M0_STRINGARG('S', "Storage domain name",
				LAMBDA(void, (const char *str)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_stpath = str;
				})),
			M0_STRINGARG('d', "device configuration file",
				LAMBDA(void, (const char *str)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rctx->rc_dfilepath = str;
				})),
			M0_STRINGARG('e', "Network endpoint,"
				     " e.g. transport:address",
				LAMBDA(void, (const char *str)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					rc = ep_and_xprt_append(rctx, str);
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
			M0_STRINGARG('s', "Services to be configured",
				LAMBDA(void, (const char *str)
				{
					_RETURN_EINVAL_UNLESS(rctx);
					if (rctx->rc_snr >=
					    rctx->rc_max_services) {
						rc = -E2BIG;
						M0_ADDB_ADD(&cctx->cc_addb,
							    &cs_addb_loc,
							    arg_fail,
							    "Too many services",
							    rc);
						return;
					}
					rctx->rc_services[rctx->rc_snr] = str;
					M0_CNT_INC(rctx->rc_snr);
				})));
#undef _RETURN_EINVAL_UNLESS
	return result ?: rc;
}

int m0_cs_setup_env(struct m0_mero *cctx, int argc, char **argv)
{
	int rc;

	M0_PRE(cctx != NULL);

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	m0_rwlock_write_lock(&cctx->cc_rwlock);
	rc = cs_parse_args(cctx, argc, argv) ?:
		reqh_ctxs_are_valid(cctx) ?:
		cs_net_domains_init(cctx) ?:
		cs_buffer_pool_setup(cctx) ?:
		cs_request_handlers_start(cctx) ?:
		cs_rpc_machines_init(cctx);
	m0_rwlock_write_unlock(&cctx->cc_rwlock);

	if (rc < 0) {
		M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc, setup_fail,
			    "m0_cs_setup_env", rc);
		cs_usage(cctx->cc_outfile);
	}

	return rc;
}

int m0_cs_start(struct m0_mero *cctx)
{
	int rc;

	M0_PRE(cctx != NULL);

	rc = cs_services_init(cctx);
	if (rc != 0)
		M0_ADDB_ADD(&cctx->cc_addb, &cs_addb_loc,
			    service_init_fail,
			    "m0_cs_start", rc);
	return rc;
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
