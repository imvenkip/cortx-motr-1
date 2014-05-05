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
#include "lib/string.h"     /* m0_strdup, m0_streq */
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/uuid.h"
#include "lib/locality.h"
#include "balloc/balloc.h"
#include "stob/ad.h"
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
#include "addb/addb_monitor.h"
#include "module/instance.h"	/* m0_get */

#include "be/ut/helper.h"

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

M0_INTERNAL const uint64_t m0_addb_stob_key = M0_ADDB_STOB_KEY;

static bool reqh_context_check(const void *bob);

static struct m0_bob_type rhctx_bob = {
	.bt_name         = "m0_reqh_context",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_reqh_context, rc_magix),
	.bt_magix        = M0_CS_REQH_CTX_MAGIC,
	.bt_check        = reqh_context_check
};
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

static bool reqh_ctx_args_are_valid(const struct m0_reqh_context *rctx)
{
	return equi(rctx->rc_confdb != NULL,
			m0_exists(i, rctx->rc_nr_services,
				  m0_streq(rctx->rc_services[i], "confd"))) &&
		rctx->rc_stype != NULL && rctx->rc_stpath != NULL &&
		rctx->rc_addb_stlocation != NULL && rctx->rc_dbpath != NULL &&
		rctx->rc_nr_services != 0 && rctx->rc_services != NULL &&
		!cs_eps_tlist_is_empty(&rctx->rc_eps);
}

static bool reqh_context_check(const void *bob)
{
	const struct m0_reqh_context *rctx = bob;
	return
		_0C(M0_IN(rctx->rc_state,
			  (RC_UNINITIALISED, RC_INITIALISED))) &&
		_0C(rctx->rc_max_services == m0_reqh_service_types_length()) &&
		_0C(M0_CHECK_EX(m0_tlist_invariant(&cs_eps_tl,
						   &rctx->rc_eps))) &&
		_0C(rctx->rc_stype == NULL || reqh_ctx_args_are_valid(rctx)) &&
		_0C(rctx->rc_mero != NULL) &&
		_0C(ergo(rctx->rc_state == RC_INITIALISED,
			 m0_reqh_invariant(&rctx->rc_reqh)));
}

static bool reqh_context_invariant(const struct m0_reqh_context *rctx)
{
	return m0_reqh_context_bob_check(rctx); /* calls reqh_context_check() */
}

M0_INTERNAL struct m0_rpc_machine *m0_mero_to_rmach(struct m0_mero *mero)
{
	return m0_reqh_rpc_mach_tlist_head(
		&mero->cc_reqh_ctx.rc_reqh.rh_rpc_machines);
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
		if (m0_streq(xprt_name, xprts[i]->nx_name))
			return xprts[i];
	return NULL;
}

/** Lists supported network transports. */
static void cs_xprts_list(FILE *out, struct m0_net_xprt **xprts,
			  size_t xprts_nr)
{
	int i;

	M0_PRE(out != NULL && xprts != NULL);

	fprintf(out, "\nSupported transports:\n");
	for (i = 0; i < xprts_nr; ++i)
		fprintf(out, " %s\n", xprts[i]->nx_name);
}

/** Lists supported stob types. */
static void cs_stob_types_list(FILE *out)
{
	int i;

	M0_PRE(out != NULL);

	fprintf(out, "\nSupported stob types:\n");
	for (i = 0; i < ARRAY_SIZE(m0_cs_stypes); ++i)
		fprintf(out, " %s\n", m0_cs_stypes[i]);
}

/** Checks if the specified storage type is supported in a mero context. */
static bool stype_is_valid(const char *stype)
{
	M0_PRE(stype != NULL);

	return  strcasecmp(stype, m0_cs_stypes[M0_AD_STOB]) == 0 ||
		strcasecmp(stype, m0_cs_stypes[M0_LINUX_STOB]) == 0;
}

/**
   Checks if given network transport and network endpoint address are already
   in use in a request handler context.
 */
static bool cs_endpoint_is_duplicate(const struct m0_reqh_context *rctx,
				     const struct m0_net_xprt *xprt,
				     const char *ep)
{
	static int (*cmp[])(const char *s1, const char *s2) = {
		strcmp,
		m0_net_lnet_ep_addr_net_cmp
	};
	struct cs_endpoint_and_xprt *ex;
	bool                         seen = false;

	M0_PRE(reqh_context_invariant(rctx) && ep != NULL);

	m0_tl_for(cs_eps, &rctx->rc_eps, ex) {
		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ex));
		if (cmp[!!m0_streq(xprt->nx_name, "lnet")](ex->ex_endpoint,
							   ep) == 0 &&
		    m0_streq(ex->ex_xprt, xprt->nx_name)) {
			if (seen)
				return true;
			else
				seen = true;
		}
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
		return M0_RC(-EINVAL);

	xprt = cs_xprt_lookup(xprt_name, cctx->cc_xprts, cctx->cc_xprts_nr);
	if (xprt == NULL)
		return M0_RC(-EINVAL);

	return M0_RC(cs_endpoint_is_duplicate(&cctx->cc_reqh_ctx, xprt, ep) ?
		     -EADDRINUSE : 0);
}

M0_INTERNAL int m0_ep_and_xprt_extract(struct cs_endpoint_and_xprt *epx,
				       const char *ep)
{
	char *sptr;
	char *endpoint;
	int   ep_len = min32u(strlen(ep) + 1, CS_MAX_EP_ADDR_LEN);

	M0_PRE(ep != NULL);

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

	rc = m0_ep_and_xprt_extract(epx, ep);
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

	M0_PRE(reqh_context_invariant(rctx));

	for (i = 0, n = 0; i < rctx->rc_nr_services; ++i) {
		if (strcasecmp(rctx->rc_services[i], sname) == 0)
			++n;
		if (n > 1)
			return true;
	}
	return false;
}

static int cs_reqh_ctx_init(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;

	M0_ENTRY();

	*rctx = (struct m0_reqh_context){
		.rc_max_services = m0_reqh_service_types_length(),
		.rc_mero         = cctx
	};
	if (rctx->rc_max_services == 0)
		return M0_ERR(-EINVAL, "No services registered");

	M0_ALLOC_ARR(rctx->rc_services,      rctx->rc_max_services);
	M0_ALLOC_ARR(rctx->rc_service_uuids, rctx->rc_max_services);
	if (rctx->rc_services == NULL || rctx->rc_service_uuids == NULL) {
		m0_free(rctx->rc_services);
		m0_free(rctx->rc_service_uuids);
		return M0_RC(-ENOMEM);
	}

	cs_eps_tlist_init(&rctx->rc_eps);
	m0_reqh_context_bob_init(rctx);

	return M0_RC(0);
}

static void cs_reqh_ctx_fini(struct m0_reqh_context *rctx)
{
	struct cs_endpoint_and_xprt *ep;
	int                          i;

	m0_reqh_context_bob_fini(rctx);

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
}

M0_INTERNAL struct m0_net_domain *
m0_cs_net_domain_locate(struct m0_mero *cctx, const char *xprt_name)
{
	struct m0_net_domain *ndom;

	M0_PRE(cctx != NULL && xprt_name != NULL);

	m0_tl_for(ndom, &cctx->cc_ndoms, ndom) {
		M0_ASSERT(m0_net_domain_bob_check(ndom));
		if (m0_streq(ndom->nd_xprt->nx_name, xprt_name))
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
	if (rc != 0)
		m0_free(rpcmach);
	return rc;
}

static int cs_rpc_machines_init(struct m0_mero *cctx)
{
	struct m0_reqh_context      *rctx = &cctx->cc_reqh_ctx;
	struct cs_endpoint_and_xprt *ep;

	M0_ENTRY();
	M0_PRE(reqh_context_invariant(rctx));

	m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
		int rc;

		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		rc = cs_rpc_machine_init(cctx, ep->ex_xprt,
					 ep->ex_endpoint, ep->ex_tm_colour,
					 rctx->rc_recv_queue_min_length,
					 rctx->rc_max_rpc_msg_size,
					 &rctx->rc_reqh);
		if (rc != 0)
			return M0_RC(rc);
	} m0_tl_endfor;

	return M0_RC(0);
}

static void cs_rpc_machines_fini(struct m0_reqh *reqh)
{
	struct m0_rpc_machine *rpcmach;

	m0_tl_for(m0_reqh_rpc_mach, &reqh->rh_rpc_machines, rpcmach) {
		M0_ASSERT(m0_rpc_machine_bob_check(rpcmach));
		m0_rpc_machine_fini(rpcmach);
		m0_free(rpcmach);
	} m0_tl_endfor;
}

static uint32_t
cs_domain_tms_nr(struct m0_reqh_context *rctx, struct m0_net_domain *dom)
{
	struct cs_endpoint_and_xprt *ep;
	uint32_t                     n = 0;

	m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
		if (m0_streq(ep->ex_xprt, dom->nd_xprt->nx_name))
			ep->ex_tm_colour = n++;
	} m0_tl_endfor;

	M0_POST(n > 0);
	return n;
}

static uint32_t cs_dom_tm_min_recv_queue_total(struct m0_reqh_context *rctx,
					       struct m0_net_domain *dom)
{
	struct cs_endpoint_and_xprt *ep;
	uint32_t                     result = 0;

	M0_PRE(reqh_context_invariant(rctx));

	m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
		if (m0_streq(ep->ex_xprt, dom->nd_xprt->nx_name))
			result += rctx->rc_recv_queue_min_length;
	} m0_tl_endfor;
	return result;
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
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;
	struct m0_net_domain   *dom;
	struct cs_buffer_pool  *bp;
	uint32_t                tms_nr;
	uint32_t                max_recv_queue_len;
	int                     rc = 0;

	m0_tl_for(ndom, &cctx->cc_ndoms, dom) {
		max_recv_queue_len = cs_dom_tm_min_recv_queue_total(rctx, dom);
		tms_nr = cs_domain_tms_nr(rctx, dom);
		M0_ASSERT(max_recv_queue_len >= tms_nr);

		M0_ALLOC_PTR(bp);
		if (bp == NULL) {
			rc = -ENOMEM;
			break;
		}
		rc = m0_rpc_net_buffer_pool_setup(
			dom, &bp->cs_buffer_pool,
			m0_rpc_bufs_nr(max_recv_queue_len, tms_nr),
			tms_nr);
		if (rc != 0) {
			m0_free(bp);
			break;
		}
		cs_buffer_pools_tlink_init_at_tail(bp, &cctx->cc_buffer_pools);
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

/* XXX DESCRIBEME cid is linux_stob_key and ad_dom_key */
static int cs_ad_stob_create(struct cs_stobs *stob, uint64_t cid,
			     struct m0_be_seg *seg, const char *f_path)
{
	int                rc;
	char               location[MAXPATHLEN];
	char              *dom_cfg;
	struct m0_stob    *bstore;
	struct cs_ad_stob *adstob;

	M0_ENTRY("cid=%llu path=%s", (unsigned long long)cid, f_path);

	M0_ALLOC_PTR(adstob);
	if (adstob == NULL)
		return M0_ERR(-ENOMEM, "adstob object allocation failed");

	rc = m0_stob_find_by_key(stob->s_sdom, cid, &bstore);
	adstob->as_stob_back = bstore;
	rc = rc ?: m0_stob_locate(bstore);
	if (rc == 0 && m0_stob_state_get(bstore) == CSS_NOENT) {
		/* XXX assume that whole cfg_str is a symlink if != NULL */
		rc = m0_stob_create(bstore, NULL, f_path);
	}

	if (rc == 0) {
		/* XXX fix when seg0 is landed */
		rc = snprintf(location, sizeof(location),
			      "adstob:seg=%p,ad.%lx", seg, cid);
		M0_ASSERT(rc < sizeof(location));
		m0_stob_ad_cfg_make(&dom_cfg, seg, m0_stob_fid_get(bstore));
		if (dom_cfg == NULL) {
			rc = -ENOMEM;
		} else {
			rc = m0_stob_domain_create_or_init(location, NULL,
							   cid, dom_cfg,
							   &adstob->as_dom);
		}
		m0_free(dom_cfg);

		if (rc == 0 && M0_FI_ENABLED("ad_domain_locate_fail")) {
			m0_stob_domain_fini(adstob->as_dom);
			rc = -EINVAL;
		}

		if (rc == 0) {
			cs_ad_stob_bob_init(adstob);
			astob_tlink_init_at_tail(adstob, &stob->s_adstobs);
		}
	}

	if (rc != 0) {
		if (bstore != NULL)
			m0_stob_put(bstore);
		m0_free(adstob);
	}
	return M0_RC(rc);
}

static void cs_ad_stob_fini(struct cs_stobs *stob)
{
	struct cs_ad_stob *adstob;

	M0_PRE(stob != NULL);

	m0_tl_for(astob, &stob->s_adstobs, adstob) {
		M0_ASSERT(cs_ad_stob_bob_check(adstob));
		M0_ASSERT(adstob->as_dom != NULL);
		m0_stob_domain_fini(adstob->as_dom);
		m0_stob_put(adstob->as_stob_back);
		astob_tlink_del_fini(adstob);
		cs_ad_stob_bob_fini(adstob);
		m0_free(adstob);
	} m0_tl_endfor;
	astob_tlist_fini(&stob->s_adstobs);
}

/**
   Initialises AD type stob.
 */
static int cs_ad_stob_init(struct cs_stobs *stob, struct m0_be_seg *db)
{
	int		  rc;
	int		  result;
	uint64_t	  cid;
	const char       *f_path;
	yaml_document_t  *doc;
	yaml_node_t      *node;
	yaml_node_t      *s_node;
	yaml_node_item_t *item;


	M0_ENTRY();

	astob_tlist_init(&stob->s_adstobs);
	if (stob->s_sfile.sf_is_initialised) {
		doc = &stob->s_sfile.sf_document;
		rc = 0;
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
		m0_get()->i_reqh_has_multiple_ad_domains = true;
	} else {
		rc = cs_ad_stob_create(stob, M0_AD_STOB_KEY_DEFAULT, db, NULL);
		m0_get()->i_reqh_has_multiple_ad_domains = false;
	}

	if (rc != 0)
		cs_ad_stob_fini(stob);

	return M0_RC(rc);
}

/**
   Initialises storage including database environment and stob domain of given
   type (e.g. linux or ad). There is a stob domain and a database environment
   created per request handler context.

   @todo Use generic mechanism to generate stob ids
 */
/* XXX rewrite stob_type */
static int cs_storage_init(const char *stob_type,
			   const char *stob_path,
			   uint64_t dom_key,
			   struct cs_stobs *stob,
			   struct m0_be_seg *db,
			   bool mkfs)
{
	int                rc;
	size_t             slen;
	char              *location;
	static const char  prefix[] = "linuxstob:";

	M0_ENTRY();

	M0_PRE(stob_type != NULL && stob_path != NULL && stob != NULL);

	if (strcasecmp(stob_type, m0_cs_stypes[M0_LINUX_STOB]) == 0) {
		stob->s_stype = M0_LINUX_STOB;
		m0_get()->i_reqh_uses_ad_stob = false;
	} else if (strcasecmp(stob_type, m0_cs_stypes[M0_AD_STOB]) == 0) {
		stob->s_stype = M0_AD_STOB;
		m0_get()->i_reqh_uses_ad_stob = true;
	} else
		return M0_RC(-EINVAL);

	slen = strlen(stob_path);
	M0_ALLOC_ARR(location, slen + ARRAY_SIZE(prefix));
	if (location == NULL)
		return M0_RC(-ENOMEM);

	sprintf(location, "%s%s", prefix, stob_path);
	if (mkfs) {
		rc = m0_stob_domain_init(location, "directio=true",
					 &stob->s_sdom);
		if (rc == 0) {
			/* Found existing stob domain, kill it. */
			m0_stob_domain_destroy(stob->s_sdom);
		}
		rc = m0_stob_domain_create_or_init(location, "directio=true",
						   dom_key, NULL, &stob->s_sdom);
		if (rc != 0)
			M0_LOG(M0_ERROR, "m0_stob_domain_create_or_init: rc=%d", rc);
	} else {
		rc = m0_stob_domain_init(location, "directio=true",
					 &stob->s_sdom);
		if (rc != 0)
			M0_LOG(M0_ERROR, "m0_stob_domain_init: rc=%d", rc);
	}
	m0_free(location);

	if (rc == 0 && strcasecmp(stob_type, m0_cs_stypes[M0_AD_STOB]) == 0) {
		rc = cs_ad_stob_init(stob, db);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "cs_ad_stob_init: rc=%d", rc);
			m0_stob_domain_fini(stob->s_sdom);
		}
	}
	return M0_RC(rc);
}

/**
   Finalises storage for a request handler in a mero context.
 */
static void cs_storage_fini(struct cs_stobs *stob)
{
	if (stob->s_stype == M0_AD_STOB)
		cs_ad_stob_fini(stob);
	if (stob->s_sdom != NULL)
		m0_stob_domain_fini(stob->s_sdom);
	if (stob->s_sfile.sf_is_initialised)
		yaml_document_delete(&stob->s_sfile.sf_document);
}

static int __service_init(const char *name, struct m0_reqh_context *rctx,
			  struct m0_reqh *reqh, struct m0_uint128 *uuid,
			  bool mgmt)
{
	struct m0_reqh_service_type *stype;
	struct m0_reqh_service      *service;
	int                          rc;

	M0_ENTRY("name=`%s'", name);
	M0_PRE(name != NULL && *name != '\0' && reqh != NULL);

	stype = m0_reqh_service_type_find(name);
	if (stype == NULL)
		return M0_RC(-EINVAL);

	rc = m0_reqh_service_allocate(&service, stype, rctx);
	if (rc != 0)
		return M0_RC(rc);

	m0_reqh_service_init(service, reqh, uuid);

	/** @todo Remove the USE_MGMT_STARTUP macro later */
	rc = mgmt ? m0_mgmt_reqh_service_start(service) :
		m0_reqh_service_start(service);

	if (rc != 0)
		m0_reqh_service_fini(service);

	M0_POST(ergo(rc == 0, m0_reqh_service_invariant(service)));
	return M0_RC(rc);

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
	/** @todo XXX Remove the USE_MGMT_STARTUP macro later */
#define USE_MGMT_STARTUP 0
#if USE_MGMT_STARTUP
	return __service_init(name, rctx, reqh, uuid, true);
#else
	return __service_init(name, rctx, reqh, uuid, false);
#endif
}

static int reqh_context_services_init(struct m0_reqh_context *rctx)
{
	const char *name;
	uint32_t    i;
	int         rc;

	M0_ENTRY();
	M0_PRE(reqh_context_invariant(rctx));

	for (i = 0, rc = 0; i < rctx->rc_nr_services && rc == 0; ++i) {
		name = rctx->rc_services[i];
		rc = cs_service_init(name, rctx, &rctx->rc_reqh,
				     &rctx->rc_service_uuids[i]);
	}
#if USE_MGMT_STARTUP
	/* Do not terminate on failure here as services start asynchronously. */
#else
	if (rc != 0)
		m0_reqh_pre_storage_fini_svcs_stop(&rctx->rc_reqh);
#endif
	return M0_RC(rc);
}

#if USE_MGMT_STARTUP
static int reqh_services_start(struct m0_reqh_context *rctx)
{
	struct m0_reqh *reqh = &rctx->rc_reqh;
	int             rc;

	M0_ENTRY();

	rc = m0_reqh_mgmt_service_start(reqh);
	if (rc != 0)
		return M0_RC(rc);

	m0_reqh_start(reqh);

	rc = cs_service_init("simple-fom-service", NULL, reqh, NULL) ?:
		reqh_context_services_init(rctx);

	m0_mgmt_reqh_services_start_wait(reqh);

	if (rc != 0)
		return M0_RC(rc);

	return M0_RC(m0_reqh_services_state_count(reqh, M0_RST_FAILED) > 0 ?
		     -ENOEXEC : 0);
}
#else
static int reqh_services_start(struct m0_reqh_context *rctx)
{
	struct m0_reqh *reqh = &rctx->rc_reqh;
	int             rc;

	M0_ENTRY();

	rc = m0_reqh_mgmt_service_start(reqh) ?:
		cs_service_init("simple-fom-service", NULL, reqh, NULL) ?:
		reqh_context_services_init(rctx);

	if (rc == 0)
		m0_reqh_start(reqh);

	return M0_RC(rc);
}
#endif /* !USE_MGMT_STARTUP */

static int
cs_net_domain_init(struct cs_endpoint_and_xprt *ep, struct m0_mero *cctx)
{
	struct m0_net_xprt   *xprt;
	struct m0_net_domain *ndom = NULL;
	int                   rc;

	M0_PRE(cs_endpoint_and_xprt_bob_check(ep));

	xprt = cs_xprt_lookup(ep->ex_xprt, cctx->cc_xprts, cctx->cc_xprts_nr);
	if (xprt == NULL)
		return -EINVAL;

	ndom = m0_cs_net_domain_locate(cctx, ep->ex_xprt);
	if (ndom != NULL)
		return 0; /* pass */

	rc = m0_net_xprt_init(xprt);
	if (rc != 0)
		return rc;

	M0_ALLOC_PTR(ndom);
	if (ndom == NULL) {
		rc = -ENOMEM;
		goto err;
	}

	/** @todo replace m0_addb_proc_ctx */
	rc = m0_net_domain_init(ndom, xprt, &m0_addb_proc_ctx);
	if (rc != 0)
		goto err;

	m0_net_domain_bob_init(ndom);
	ndom_tlink_init_at_tail(ndom, &cctx->cc_ndoms);
	return 0;
err:
	m0_free(ndom); /* freeing NULL does not hurt */
	m0_net_xprt_fini(xprt);
	return rc;
}

/**
   Initialises network domains per given distinct xport:endpoint pair in a
   mero context.
 */
static int cs_net_domains_init(struct m0_mero *cctx)
{
	struct m0_reqh_context      *rctx = &cctx->cc_reqh_ctx;
	struct cs_endpoint_and_xprt *ep;

	M0_ENTRY();
	M0_PRE(reqh_context_invariant(rctx));

	m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
		int rc = cs_net_domain_init(ep, cctx);
		if (rc != 0)
			return M0_RC(rc);
	} m0_tl_endfor;
	return M0_RC(0);
}

/**
   Finalises all the network domains within a mero context.

   @param cctx Mero context to which the network domains belong
 */
static void cs_net_domains_fini(struct m0_mero *cctx)
{
	struct m0_net_domain *ndom;
	size_t                i;

	m0_tl_for(ndom, &cctx->cc_ndoms, ndom) {
		M0_ASSERT(m0_net_domain_bob_check(ndom));
		m0_net_domain_fini(ndom);
		ndom_tlink_del_fini(ndom);
		m0_net_domain_bob_fini(ndom);
		m0_free(ndom);
	} m0_tl_endfor;

	for (i = 0; i < cctx->cc_xprts_nr; ++i)
		m0_net_xprt_fini(cctx->cc_xprts[i]);
}

static int cs_storage_prepare(struct m0_reqh_context *rctx, bool erase)
{
	struct m0_sm_group   *grp = m0_locality0_get()->lo_grp;
	struct m0_cob_domain *dom = &rctx->rc_mdstore.md_dom;
	struct m0_dtx         tx = {};
	int                   rc;

	m0_sm_group_lock(grp);

	if (erase)
		m0_mdstore_destroy(&rctx->rc_mdstore, grp);

	rc = m0_mdstore_create(&rctx->rc_mdstore, grp);
	if (rc != 0)
		goto end;

	m0_dtx_init(&tx, rctx->rc_beseg->bs_domain, grp);
	m0_cob_tx_credit(dom, M0_COB_OP_DOMAIN_MKFS, &tx.tx_betx_cred);

	rc = m0_dtx_open_sync(&tx);
	if (rc == 0) {
		rc = m0_cob_domain_mkfs(dom, &M0_MDSERVICE_SLASH_FID, &tx.tx_betx);
		if (rc != 0)
			m0_cob_domain_destroy(dom, grp);
		m0_dtx_done_sync(&tx);
	}
	m0_dtx_fini(&tx);
end:
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
static int cs_addb_storage_init(struct m0_reqh_context *rctx, bool mkfs)
{
	struct cs_addb_stob *addb_stob = &rctx->rc_addb_stob;
	struct m0_stob	    *stob;
	int		     rc;

	M0_ENTRY();

	if (mkfs) {
		rc = m0_stob_domain_init(rctx->rc_addb_stlocation, NULL,
					 &addb_stob->cas_stobs.s_sdom);
		if (rc == 0) {
			/* Found existing stob domain, kill it. */
			m0_stob_domain_destroy(addb_stob->cas_stobs.s_sdom);
		}
		/** @todo allow different stob type for data stobs & ADDB stobs? */
		rc = m0_stob_domain_create_or_init(rctx->rc_addb_stlocation,
						   NULL, 0, NULL,
						   &addb_stob->cas_stobs.s_sdom);
	} else {
		rc = m0_stob_domain_init(rctx->rc_addb_stlocation, NULL,
					 &addb_stob->cas_stobs.s_sdom);
	}
	if (rc != 0)
		return M0_RC(rc);

	rc = m0_stob_find_by_key(rctx->rc_addb_stob.cas_stobs.s_sdom,
				 m0_addb_stob_key, &stob);
	if (rc == 0) {
		rc = m0_stob_locate(stob);
		rc = rc ?: m0_stob_state_get(stob) == CSS_EXISTS ? 0 :
			   m0_stob_create(stob, NULL, NULL);
		if (rc != 0)
			m0_stob_put(stob);
		addb_stob->cas_stob = stob;
	}
	if (rc != 0)
		m0_stob_domain_fini(addb_stob->cas_stobs.s_sdom);
	return M0_RC(rc);
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
static int cs_reqh_start(struct m0_reqh_context *rctx, bool mkfs)
{
	int rc;

	M0_ENTRY();
	M0_PRE(reqh_context_invariant(rctx));

	/** @todo Pass in a parent ADDB context for the db. Ideally should
	    be same parent as that of the reqh.
	    But, we'd also want the db to use the same addb m/c as the reqh.
	    Needs work.
	 */

	rc = M0_REQH_INIT(&rctx->rc_reqh, .rhia_mdstore = &rctx->rc_mdstore);
	if (rc != 0)
		goto out;

	rctx->rc_db.d_i.d_ut_be.but_dom_cfg.bc_engine.bec_group_fom_reqh =
		&rctx->rc_reqh;

	rc = m0_dbenv_init(&rctx->rc_db, rctx->rc_dbpath, 0, mkfs);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_dbenv_init");
		goto reqh_fini;
	}
	rctx->rc_beseg = rctx->rc_db.d_i.d_seg;
	rctx->rc_reqh.rh_dbenv = &rctx->rc_db;

	rc = m0_reqh_dbenv_init(&rctx->rc_reqh, rctx->rc_beseg);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_reqh_dbenv_init: rc=%d", rc);
		goto dbenv_fini;
	}

	if (rctx->rc_dfilepath != NULL) {
		rc = cs_stob_file_load(rctx->rc_dfilepath, &rctx->rc_stob);
		if (rc != 0) {
			M0_LOG(M0_ERROR,
			       "Failed to load device configuration file");
			goto reqh_dbenv_fini;
		}
	}

	rc = cs_storage_init(rctx->rc_stype, rctx->rc_stpath,
			     M0_AD_STOB_LINUX_DOM_KEY,
			     &rctx->rc_stob, rctx->rc_beseg,
			     mkfs);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "cs_storage_init: rc=%d", rc);
		/* XXX who should call yaml_document_delete()? */
		goto reqh_dbenv_fini;
	}

	rc = cs_addb_storage_init(rctx, mkfs);
	if (rc != 0)
		goto cleanup_stob;

	rc = m0_reqh_addb_mc_config(&rctx->rc_reqh,
				    rctx->rc_addb_stob.cas_stob);
	if (rc != 0)
		goto cleanup_addb_stob;

	rctx->rc_cdom_id.id = ++cdom_id;

	if (mkfs) {
		/*
		 * Init mdstore without root cob first. Now we can use it
        	 * for mkfs.
		 */
		rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id,
				     rctx->rc_beseg, false);
		if (rc != 0 && rc != -ENOENT) {
			M0_LOG(M0_ERROR, "m0_mdstore_init: rc=%d", rc);
			goto cleanup_addb_stob;
		}

		/* Prepare new metadata structure, erase old one if exists. */
		rc = cs_storage_prepare(rctx, rc == 0);
		m0_mdstore_fini(&rctx->rc_mdstore);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "cs_storage_prepare: rc=%d", rc);
			goto cleanup_addb_stob;
		}
	}
	/* Init mdstore and root cob as it should be created by mkfs. */
	rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id,
			     rctx->rc_beseg, true);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Failed to initialize mdstore. %s", !mkfs ? "Did you run mkfs?" : "Mkfs failed?");
		goto cleanup_addb_stob;
	}

	rctx->rc_state = RC_INITIALISED;
	return M0_RC(rc);

cleanup_addb_stob:
	cs_addb_storage_fini(&rctx->rc_addb_stob);
cleanup_stob:
	cs_storage_fini(&rctx->rc_stob);
reqh_dbenv_fini:
	m0_reqh_dbenv_fini(&rctx->rc_reqh);
dbenv_fini:
	m0_dbenv_fini(&rctx->rc_db);
reqh_fini:
	m0_reqh_fini(&rctx->rc_reqh);
out:
	M0_ASSERT(rc != 0);
	return M0_RC(rc);
}

/**
   Finalises a request handler context.
   Sets m0_reqh::rh_shutdown true, and checks if the request handler can be
   shutdown by invoking m0_reqh_can_shutdown().
   This waits until m0_reqh_can_shutdown() returns true and then proceeds for
   further cleanup.

   @param rctx Request handler context to be finalised

   @pre reqh_context_invariant()
 */
static void cs_reqh_stop(struct m0_reqh_context *rctx)
{
	struct m0_reqh *reqh = &rctx->rc_reqh;

	M0_ENTRY();
	M0_PRE(rctx->rc_state == RC_INITIALISED);
	M0_PRE(reqh_context_invariant(rctx));

	if (reqh->rh_addb_monitoring_ctx.amc_stats_conn != NULL)
		m0_addb_monitor_stats_svc_conn_fini(reqh);

	if (m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL)
		m0_reqh_shutdown_wait(reqh);

	if (m0_reqh_state_get(reqh) == M0_REQH_ST_DRAIN ||
	    m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED ||
	    m0_reqh_state_get(reqh) == M0_REQH_ST_INIT)
		m0_reqh_pre_storage_fini_svcs_stop(reqh);

	if (m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STOP)
		m0_reqh_mgmt_service_stop(reqh);

	M0_ASSERT(m0_reqh_state_get(reqh) == M0_REQH_ST_STOPPED);
	m0_reqh_dbenv_fini(reqh);
	m0_mdstore_fini(&rctx->rc_mdstore);
	cs_addb_storage_fini(&rctx->rc_addb_stob);
	cs_storage_fini(&rctx->rc_stob);
	m0_dbenv_fini(&rctx->rc_db);
	m0_reqh_post_storage_fini_svcs_stop(reqh);
	cs_rpc_machines_fini(reqh);
	m0_reqh_fini(reqh);

	rctx->rc_state = RC_UNINITIALISED;
	M0_LEAVE();
}

struct m0_reqh *m0_cs_reqh_get(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;

	m0_rwlock_read_lock(&cctx->cc_rwlock);
	M0_ASSERT(reqh_context_invariant(rctx));
	m0_rwlock_read_unlock(&cctx->cc_rwlock);

	return &rctx->rc_reqh;
}
M0_EXPORTED(m0_cs_reqh_get);

M0_INTERNAL struct m0_mero *m0_cs_ctx_get(struct m0_reqh *reqh)
{
	return bob_of(reqh, struct m0_reqh_context, rc_reqh,
		      &rhctx_bob)->rc_mero;
}

static void cs_mero_init(struct m0_mero *cctx)
{
	ndom_tlist_init(&cctx->cc_ndoms);
	m0_bob_type_tlist_init(&ndom_bob, &ndom_tl);
	cs_buffer_pools_tlist_init(&cctx->cc_buffer_pools);

	m0_bob_type_tlist_init(&cs_eps_bob, &cs_eps_tl);

	m0_bob_type_tlist_init(&astob_bob, &astob_tl);
	m0_rwlock_init(&cctx->cc_rwlock);

	cs_eps_tlist_init(&cctx->cc_ios_eps);
	cctx->cc_args.ca_argc = 0;
}

static void cs_mero_fini(struct m0_mero *cctx)
{
	struct cs_endpoint_and_xprt *ep;

	m0_tl_for(cs_eps, &cctx->cc_ios_eps, ep) {
		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		M0_ASSERT(ep->ex_scrbuf != NULL);
		m0_free(ep->ex_scrbuf);
		cs_eps_tlink_del_fini(ep);
		cs_endpoint_and_xprt_bob_fini(ep);
		m0_free(ep);
	} m0_tl_endfor;

	cs_eps_tlist_fini(&cctx->cc_ios_eps);

	cs_buffer_pools_tlist_fini(&cctx->cc_buffer_pools);
	ndom_tlist_fini(&cctx->cc_ndoms);
	m0_rwlock_fini(&cctx->cc_rwlock);

	while (cctx->cc_args.ca_argc > 0)
		m0_free(cctx->cc_args.ca_argv[--cctx->cc_args.ca_argc]);
}

static void cs_usage(FILE *out, const char *progname)
{
	M0_PRE(out != NULL);
	M0_PRE(progname != NULL);

	fprintf(out,
"Usage: %s [-h] [-x] [-l]\n"
"    or %s <global options> <reqh>+\n"
"\n"
"Type `%s -h' for help.\n", progname, progname, progname);
}

static void cs_help(FILE *out, const char *progname)
{
	M0_PRE(out != NULL);

	cs_usage(out, progname);
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
"  -R addr  Stats service endpoint address \n"
"\n"
"Request handler options:\n"
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
"           If multiple '-e' options are provided, network transport\n"
"           will have several endpoints, distinguished by transfer machine id\n"
"           (the 4th component of 4-tuple endpoint address in lnet).\n"
"  -s str   Service (type) to be started.\n"
"           The string is of one of the following forms:\n"
"              ServiceTypeName:ServiceInstanceUUID\n"
"              ServiceTypeName\n"
"           with the UUID expressed in the standard 8-4-4-4-12 hexadecimal\n"
"           string form. The non-UUID form is permitted for testing purposes.\n"
"           Multiple '-s' options are allowed, but the values must be unique.\n"
"           Use '-l' to get a list of registered service types.\n"
"  -q num   [optional] Minimum length of TM receive queue.\n"
"           Defaults to the value set with '-Q' option.\n"
"  -m num   [optional] Maximum RPC message size.\n"
"           Defaults to the value set with '-M' option.\n"
"\n"
"Example:\n"
"    %s -Q 4 -M 4096 -T linux -D dbpath -S stobfile \\\n"
"        -e lnet:172.18.50.40@o2ib1:12345:34:1 -s mds -q 8 -m 65536\n", progname);
}

static int reqh_ctx_validate(struct m0_mero *cctx)
{
	struct m0_reqh_context      *rctx = &cctx->cc_reqh_ctx;
	struct cs_endpoint_and_xprt *ep;
	int                          i;
	M0_ENTRY();

	if (!reqh_ctx_args_are_valid(rctx))
		return M0_ERR(-EINVAL, "Parameters are missing or invalid");

	cctx->cc_recv_queue_min_length = max64(cctx->cc_recv_queue_min_length,
					       M0_NET_TM_RECV_QUEUE_DEF_LEN);
	rctx->rc_recv_queue_min_length = max64(rctx->rc_recv_queue_min_length,
					       M0_NET_TM_RECV_QUEUE_DEF_LEN);

	if (rctx->rc_max_rpc_msg_size == 0)
		rctx->rc_max_rpc_msg_size = cctx->cc_max_rpc_msg_size;

	if (!stype_is_valid(rctx->rc_stype)) {
		cs_stob_types_list(cctx->cc_outfile);
		return M0_ERR(-EINVAL, "Invalid service type");
	}

	if (cs_eps_tlist_is_empty(&rctx->rc_eps))
		return M0_ERR(-EINVAL, "Endpoint is missing");

	m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
		int rc;

		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		rc = cs_endpoint_validate(cctx, ep->ex_endpoint, ep->ex_xprt);
		if (rc != 0)
			return M0_ERR(rc, "Invalid endpoint: %s",
				      ep->ex_endpoint);
	} m0_tl_endfor;

	if (rctx->rc_nr_services == 0)
		return M0_ERR(-EINVAL, "No service specified");

	for (i = 0; i < rctx->rc_nr_services; ++i) {
		const char *sname = rctx->rc_services[i];

		if (!m0_reqh_service_is_registered(sname))
			return M0_ERR(-ENOENT, "Service is not registered: %s",
				      sname);

		if (service_is_duplicate(rctx, sname))
			return M0_ERR(-EEXIST, "Service is not unique: %s",
				      sname);
	}
	return M0_RC(0);
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
	if (cctx->cc_daemon) {
		struct sigaction hup_act = { .sa_handler = SIG_IGN };
		return daemon(1, 0) ?: sigaction(SIGHUP, &hup_act, NULL);
	}
	return 0;
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
static int
service_string_parse(const char *str, char **svc, struct m0_uint128 *uuid)
{
	const char *colon;
	size_t      len;
	int         rc;

	uuid->u_lo = uuid->u_hi = 0;
	colon = strchr(str, ':');
	if (colon == NULL) {
		*svc = m0_strdup(str);
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
	if (rc != 0)
		m0_free0(svc);
	return rc;
}

/** Parses CLI arguments, filling m0_mero structure. */
static int _args_parse(struct m0_mero *cctx, int argc, char **argv,
		       const char **confd_addr, const char **profile,
		       const char **genders, bool *use_genders)
{
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;
	int                     rc_getops;
	int                     rc;

	M0_ENTRY();

	if (argc <= 1) {
		cs_usage(cctx->cc_outfile, argv[0]);
		return M0_RC(-EINVAL);
	}

	rc_getops = M0_GETOPTS(argv[0], argc, argv,
			/* -------------------------------------------
			 * Global options
			 */
			M0_VOIDARG('h', "Usage help",
				LAMBDA(void, (void)
				{
					cs_help(cctx->cc_outfile, argv[0]);
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
			M0_STRINGARG('C', "Confd endpoint address",
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
			M0_STRINGARG('G', "Mdservice endpoint address",
				LAMBDA(void, (const char *s)
				{
					rc = m0_ep_and_xprt_extract(
						&cctx->cc_mds_epx, s);
				})),
			M0_STRINGARG('R', "Stats service endpoint address",
				LAMBDA(void, (const char *s)
				{
					rc = m0_ep_and_xprt_extract(
						&cctx->cc_stats_svc_epx, s);
				})),
			M0_STRINGARG('i', "Ioservice endpoints list",
				LAMBDA(void, (const char *s)
				{
					rc = ep_and_xprt_append(
						&cctx->cc_ios_eps, s);
					M0_LOG(M0_DEBUG, "adding %s to ios ep "
					       "list %d", s, rc);
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
			M0_STRINGARG('D', "Metadata storage filename",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_dbpath = s;
				})),
			M0_STRINGARG('c', "Path to the configuration database",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_confdb = s;
				})),
			M0_STRINGARG('T', "Storage domain type",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_stype = s;
				})),
			M0_STRINGARG('A', "ADDB Storage domain location",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_addb_stlocation = s;
				})),
			M0_STRINGARG('S', "Data storage filename",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_stpath = s;
				})),
			M0_STRINGARG('d', "Device configuration file",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_dfilepath = s;
				})),
			M0_NUMBERARG('q', "Minimum TM recv queue length",
				LAMBDA(void, (int64_t length)
				{
					rctx->rc_recv_queue_min_length = length;
				})),
			M0_NUMBERARG('m', "Maximum RPC message size",
				LAMBDA(void, (int64_t size)
				{
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
				      rc = ep_and_xprt_append(&rctx->rc_eps, s);
				})),
			M0_STRINGARG('s', "Services to be configured",
				LAMBDA(void, (const char *s)
				{
					int i;
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
	return M0_RC(rc_getops ?: rc);
}

static int cs_args_parse(struct m0_mero *cctx, int argc, char **argv)
{
	int                     rc;
	const char             *confd_addr = NULL;
	const char             *profile = NULL;
	const char             *genders = NULL;
	bool                    use_genders = false;

	M0_ENTRY();

	rc = _args_parse(cctx, argc, argv, &confd_addr, &profile,
			 &genders, &use_genders);
	if (rc != 0)
		return M0_RC(rc);

	if (genders != NULL && !use_genders) {
		cs_usage(cctx->cc_outfile, argv[0]);
		return M0_ERR(-EPROTO, "-f genders file specified without -g");
	}
	/**
	 * @todo allow bootstrap via genders and confd afterward, but currently
	 * confd is only used for bootstrap, thus a conflict if both present.
	 */
	if (use_genders && profile != NULL) {
		cs_usage(cctx->cc_outfile, argv[0]);
		return M0_ERR(-EPROTO, "genders use conflicts with "
			      "confd profile");
	}
	if (use_genders) {
		struct cs_args *args = &cctx->cc_args;
		bool global_daemonize = cctx->cc_daemon;

		rc = cs_genders_to_args(args, argv[0], genders);
		if (rc != 0)
			return M0_RC(rc);

		rc = _args_parse(cctx, args->ca_argc, args->ca_argv,
				 NULL, NULL, NULL, &use_genders);
		cctx->cc_daemon |= global_daemonize;
	}
	if ((confd_addr == NULL) != (profile == NULL)) {
		cs_usage(cctx->cc_outfile, argv[0]);
		return M0_ERR(-EPROTO, "%s is not specified",
			       (char *)(profile == NULL ?
			       "configuration profile" : "confd address"));
	}
	if (confd_addr != NULL) {
		struct cs_args *args = &cctx->cc_args;

		rc = cs_conf_to_args(args, confd_addr, profile);
		if (rc != 0)
			return M0_RC(rc);

		rc = _args_parse(cctx, args->ca_argc, args->ca_argv,
				 NULL, NULL, NULL, &use_genders);
	}
	if (rc == 0 && use_genders)
		rc = -EINVAL;
	return M0_RC(rc);
}

int m0_cs_setup_env(struct m0_mero *cctx, int argc, char **argv)
{
	int rc;

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	m0_rwlock_write_lock(&cctx->cc_rwlock);
	rc = cs_args_parse(cctx, argc, argv) ?:
		reqh_ctx_validate(cctx) ?:
		cs_daemonize(cctx) ?:
		cs_net_domains_init(cctx) ?:
		cs_buffer_pool_setup(cctx) ?:
		cs_reqh_start(&cctx->cc_reqh_ctx, cctx->cc_mkfs) ?:
		cs_rpc_machines_init(cctx);
	m0_rwlock_write_unlock(&cctx->cc_rwlock);

	if (rc < 0)
		M0_LOG(M0_ERROR, "m0_cs_setup_env: %d", rc);
	return rc;
}

int m0_cs_start(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;
	int                     rc;

	M0_ENTRY();
	M0_PRE(reqh_context_invariant(rctx));

	rc = reqh_services_start(rctx);

	/**
	 * @todo: stats connection initialization should be done
	 * in addb pfom, currently not being done due to some initialization
	 * ordering issues.
	 *
	 * @see http://reviewboard.clusterstor.com/r/1544
	 */
	if (cctx->cc_stats_svc_epx.ex_endpoint != NULL)
		/** @todo Log ADDB message in case of non-zero return value. */
		(void)m0_addb_monitor_stats_svc_conn_init(&rctx->rc_reqh);

	return M0_RC(rc);
}

int m0_cs_init(struct m0_mero *cctx, struct m0_net_xprt **xprts,
	       size_t xprts_nr, FILE *out, bool mkfs)
{
	M0_PRE(xprts != NULL && xprts_nr > 0 && out != NULL);

	if (M0_FI_ENABLED("fake_error"))
		return -EINVAL;

	cctx->cc_xprts    = xprts;
	cctx->cc_xprts_nr = xprts_nr;
	cctx->cc_outfile  = out;
	cctx->cc_mkfs     = mkfs;

	cs_mero_init(cctx);

	return cs_reqh_ctx_init(cctx);
}

void m0_cs_fini(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;

	M0_ENTRY();

	if (rctx->rc_state == RC_INITIALISED)
		cs_reqh_stop(rctx);
	cs_reqh_ctx_fini(rctx);

	cs_buffer_pool_fini(cctx);
	cs_net_domains_fini(cctx);
	cs_mero_fini(cctx);

	M0_LEAVE();
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
