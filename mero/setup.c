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
#include "lib/locality.h"
#include "lib/uuid.h"       /* m0_uuid_generate */
#include "fid/fid.h"
#include "stob/ad.h"
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
#include "addb2/storage.h"
#include "addb2/net.h"
#include "module/instance.h"	/* m0_get */
#include "conf/obj.h"           /* M0_CONF_PROCESS_TYPE */
#include "conf/helpers.h"       /* m0_confc_args */
#include "conf/preload.h"       /* M0_CONF_STR_MAXLEN */

#include "be/ut/helper.h"
#include "ioservice/fid_convert.h" /* M0_AD_STOB_LINUX_DOM_KEY */
#include "stob/linux.h"

/**
   @addtogroup m0d
   @{
 */

extern struct m0_reqh_service_type m0_ss_svc_type;

enum {
	CONFD_CONN_TIMEOUT = 5,
	CONFD_CONN_RETRY   = 1
};

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

M0_INTERNAL const uint64_t m0_addb_stob_key = M0_ADDB2_STOB_KEY - 1;
M0_INTERNAL const uint64_t m0_addb2_stob_key = M0_ADDB2_STOB_KEY;

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
	struct m0_mero *cctx = container_of(rctx, struct m0_mero, cc_reqh_ctx);

	return _0C(m0_exists(i, rctx->rc_nr_services,
			     m0_streq(rctx->rc_services[i], "confd")) ==
		   (rctx->rc_confdb != NULL && *rctx->rc_confdb != '\0')) &&
		_0C(!m0_exists(i, rctx->rc_nr_services,
			     m0_streq(rctx->rc_services[i], "confd")) ==
		   (cctx->cc_confd_addr != NULL && *cctx->cc_confd_addr != '\0')) &&
		_0C(rctx->rc_stype != NULL) && _0C(rctx->rc_stpath != NULL) &&
		_0C(rctx->rc_bepath != NULL) &&
		_0C(ergo(rctx->rc_nr_services != 0, rctx->rc_services != NULL &&
			 !cs_eps_tlist_is_empty(&rctx->rc_eps)));
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

static inline struct m0_confc *m0_mero2confc(struct m0_mero *mero)
{
	return &mero->cc_reqh_ctx.rc_reqh.rh_confc;
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
		return M0_ERR(-ENOMEM);
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
	return M0_ERR(-EINVAL);
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
		return M0_ERR(-ENOMEM);
	}

	rc = m0_ep_and_xprt_extract(epx, ep);
	if (rc != 0)
		goto err;

	cs_eps_tlist_add_tail(head, epx);
	return 0;
err:
	m0_free(epx);
	return M0_ERR(-EINVAL);
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
		return M0_ERR_INFO(-EINVAL, "No services registered");

	M0_ALLOC_ARR(rctx->rc_services,      rctx->rc_max_services);
	M0_ALLOC_ARR(rctx->rc_service_fids, rctx->rc_max_services);
	if (rctx->rc_services == NULL || rctx->rc_service_fids == NULL) {
		m0_free(rctx->rc_services);
		m0_free(rctx->rc_service_fids);
		return M0_ERR(-ENOMEM);
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
	m0_free(rctx->rc_service_fids);
}

M0_INTERNAL struct m0_net_domain *
m0_cs_net_domain_locate(struct m0_mero *cctx, const char *xprt_name)
{
	struct m0_net_domain *ndom;

	M0_PRE(cctx != NULL && xprt_name != NULL);

	ndom = m0_tl_find(ndom, ndom, &cctx->cc_ndoms,
			  m0_streq(ndom->nd_xprt->nx_name, xprt_name));

	M0_ASSERT(ergo(ndom != NULL, m0_net_domain_bob_check(ndom)));

	return ndom;
}

static struct m0_net_buffer_pool *
cs_buffer_pool_get(struct m0_mero *cctx, struct m0_net_domain *ndom)
{
	struct cs_buffer_pool *cs_bp;

	M0_PRE(cctx != NULL);
	M0_PRE(ndom != NULL);

	cs_bp = m0_tl_find(cs_buffer_pools, cs_bp, &cctx->cc_buffer_pools,
			   cs_bp->cs_buffer_pool.nbp_ndom == ndom);
	return cs_bp == NULL ? NULL : &cs_bp->cs_buffer_pool;
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
		return M0_ERR(-EINVAL);
	if (max_rpc_msg_size > m0_net_domain_get_max_buffer_size(ndom))
		return M0_ERR(-EINVAL);

	M0_ALLOC_PTR(rpcmach);
	if (rpcmach == NULL)
		return M0_ERR(-ENOMEM);

	buffer_pool = cs_buffer_pool_get(cctx, ndom);
	rc = m0_rpc_machine_init(rpcmach, ndom, ep,
				 reqh, buffer_pool, tm_colour, max_rpc_msg_size,
				 recv_queue_min_length);
	if (rc != 0)
		m0_free(rpcmach);
	return M0_RC(rc);
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
	return M0_RC(rc);
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
		return M0_ERR(-EINVAL);

	document = &stob->s_sfile.sf_document;
	rc = yaml_parser_initialize(&parser);
	if (rc != 1)
		return M0_ERR(-EINVAL);

	yaml_parser_set_input_file(&parser, f);
	rc = yaml_parser_load(&parser, document);
	if (rc != 1)
		return M0_ERR(-EINVAL);

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
	char               location[64];
	char              *dom_cfg;
	struct m0_stob    *bstore;
	struct cs_ad_stob *adstob;
	struct m0_stob_id  stob_id;

	M0_ENTRY("cid=%llu path=%s", (unsigned long long)cid, f_path);

	M0_ALLOC_PTR(adstob);
	if (adstob == NULL)
		return M0_ERR_INFO(-ENOMEM, "adstob object allocation failed");

	m0_stob_id_make(0, cid, &stob->s_sdom->sd_id, &stob_id);
	rc = m0_stob_find(&stob_id, &bstore);
	if (rc == 0 && m0_stob_state_get(bstore) == CSS_UNKNOWN) {
		rc = m0_stob_locate(bstore);
		adstob->as_stob_back = bstore;
	}

	if (rc == 0 && m0_stob_state_get(bstore) == CSS_NOENT) {
		/* XXX assume that whole cfg_str is a symlink if != NULL */
		rc = m0_stob_create(bstore, NULL, f_path);
	}

	if (rc == 0) {
		rc = snprintf(location, sizeof(location),
			      "adstob:%llu", (unsigned long long)cid);
		M0_ASSERT(rc < sizeof(location));
		m0_stob_ad_cfg_make(&dom_cfg, seg, m0_stob_id_get(bstore));
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
static int cs_ad_stob_init(struct cs_stobs *stob, struct m0_be_seg *seg)
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
				rc = cs_ad_stob_create(stob, cid, seg, f_path);
				if (rc != 0)
					break;
			}
		}
		m0_get()->i_reqh_has_multiple_ad_domains = true;
	} else {
		rc = cs_ad_stob_create(stob, M0_AD_STOB_DOM_KEY_DEFAULT,
				       seg, NULL);
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
			   struct m0_be_seg *seg,
			   bool mkfs, bool force)
{
	int                rc;
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

	M0_ALLOC_ARR(location, strlen(stob_path) + ARRAY_SIZE(prefix));
	if (location == NULL)
		return M0_RC(-ENOMEM);

	sprintf(location, "%s%s", prefix, stob_path);
	rc = m0_stob_domain_init(location, "directio=true", &stob->s_sdom);
	if (mkfs) {
		/* Found existing stob domain, kill it. */
		if (rc == 0 && force) {
			rc = m0_stob_domain_destroy(stob->s_sdom);
			if (rc != 0)
				goto out;
		}
		if (force || rc != 0) {
			rc = m0_stob_domain_create_or_init(location,
							   "directio=true",
							   dom_key, NULL,
							   &stob->s_sdom);
			if (rc != 0)
				M0_LOG(M0_ERROR,
				       "m0_stob_domain_create_or_init: rc=%d",
				       rc);
		} else {
			M0_LOG(M0_INFO, "Found alive filesystem, do nothing.");
		}
	} else {
		if (rc != 0)
			M0_LOG(M0_ERROR, "m0_stob_domain_init: rc=%d", rc);
	}
out:
	m0_free(location);

	if (rc == 0 && strcasecmp(stob_type, m0_cs_stypes[M0_AD_STOB]) == 0) {
		rc = cs_ad_stob_init(stob, seg);
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

/**
   Initialises and starts a particular service.

   Once the service is initialised, it is started and registered with the
   appropriate request handler.
 */
static int cs_service_init(const char *name, struct m0_reqh_context *rctx,
			   struct m0_reqh *reqh, struct m0_fid *fid)
{
	struct m0_reqh_service_type *stype;
	struct m0_reqh_service      *service;
	int                          rc;

	M0_ENTRY("name=`%s'", name);
	M0_PRE(name != NULL && *name != '\0' && reqh != NULL);

	stype = m0_reqh_service_type_find(name);
	if (stype == NULL)
		return M0_ERR(-EINVAL);

	rc = m0_reqh_service_allocate(&service, stype, rctx);
	if (rc != 0)
		return M0_RC(rc);

	m0_reqh_service_init(service, reqh, fid);

	rc = m0_reqh_service_start(service);
	if (rc != 0)
		m0_reqh_service_fini(service);

	M0_POST(ergo(rc == 0, m0_reqh_service_invariant(service)));
	return M0_RC(rc);
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
				     &rctx->rc_service_fids[i]);
	}
	if (rc != 0)
		m0_reqh_pre_storage_fini_svcs_stop(&rctx->rc_reqh);
	return M0_RC(rc);
}

static int reqh_services_start(struct m0_reqh_context *rctx)
{
	struct m0_reqh         *reqh = &rctx->rc_reqh;
	struct m0_reqh_service *ss_service;
	int                     rc;

	M0_ENTRY();

	/**
	 * @todo XXX Handle errors properly.
	 * See http://es-gerrit.xyus.xyratex.com:8080/#/c/2612/7..9/mero/setup.c
	 * for the discussion.
	 */
	rc = m0_reqh_service_setup(&ss_service, &m0_ss_svc_type,
				   reqh, NULL, NULL) ?:
		cs_service_init("simple-fom-service", NULL, reqh, NULL) ?:
		reqh_context_services_init(rctx);

	if (rc == 0)
		m0_reqh_start(reqh);

	return M0_RC(rc);
}

static int
cs_net_domain_init(struct cs_endpoint_and_xprt *ep, struct m0_mero *cctx)
{
	struct m0_net_xprt   *xprt;
	struct m0_net_domain *ndom = NULL;
	int                   rc;

	M0_PRE(cs_endpoint_and_xprt_bob_check(ep));

	xprt = cs_xprt_lookup(ep->ex_xprt, cctx->cc_xprts, cctx->cc_xprts_nr);
	if (xprt == NULL)
		return M0_ERR(-EINVAL);

	ndom = m0_cs_net_domain_locate(cctx, ep->ex_xprt);
	if (ndom != NULL)
		return 0; /* pass */

	M0_ALLOC_PTR(ndom);
	if (ndom == NULL) {
		rc = -ENOMEM;
		goto err;
	}

	rc = m0_net_domain_init(ndom, xprt);
	if (rc != 0)
		goto err;

	m0_net_domain_bob_init(ndom);
	ndom_tlink_init_at_tail(ndom, &cctx->cc_ndoms);
	return 0;
err:
	m0_free(ndom); /* freeing NULL does not hurt */
	return M0_RC(rc);
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

	m0_tl_for(ndom, &cctx->cc_ndoms, ndom) {
		M0_ASSERT(m0_net_domain_bob_check(ndom));
		m0_net_domain_fini(ndom);
		ndom_tlink_del_fini(ndom);
		m0_net_domain_bob_fini(ndom);
		m0_free(ndom);
	} m0_tl_endfor;
}

static int cs_storage_prepare(struct m0_reqh_context *rctx, bool erase)
{
	struct m0_sm_group   *grp = m0_locality0_get()->lo_grp;
	struct m0_cob_domain *dom;
	struct m0_dtx         tx = {};
	int                   rc;

	m0_sm_group_lock(grp);

	if (erase)
		m0_mdstore_destroy(&rctx->rc_mdstore, grp);

	rc = m0_mdstore_create(&rctx->rc_mdstore, grp, &rctx->rc_cdom_id,
			       rctx->rc_beseg);
	if (rc != 0)
		goto end;
	dom = rctx->rc_mdstore.md_dom;

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
	return M0_RC(rc);
}

static int cs_addb_stob_init(struct m0_reqh_context *rctx,
			     struct cs_addb_stob *addb_stob,
			     struct m0_stob_domain *dom, uint64_t key)
{
	struct m0_stob   *stob;
	int               rc;
	struct m0_stob_id stob_id;

	m0_stob_id_make(0, key, &dom->sd_id, &stob_id);
	rc = m0_stob_find(&stob_id, &stob);
	if (rc == 0) {
		rc = m0_stob_locate(stob) ?:
			m0_stob_state_get(stob) == CSS_EXISTS ? 0 :
			m0_stob_create(stob, NULL, NULL);
		if (rc == 0)
			addb_stob->cas_stob = stob;
		else
			m0_stob_put(stob);
	}
	return rc;
}

static int cs_addb_storage_init(struct m0_reqh_context *rctx,
				struct cs_addb_stob *addb_stob,
				uint64_t key, bool mkfs, bool force)
{
	struct m0_stob_domain **dom = &addb_stob->cas_stobs.s_sdom;
	int                     rc;

	M0_ENTRY();

	rc = m0_stob_domain_init(rctx->rc_addb_stlocation,
				 "directio=true", dom);
	if (mkfs) {
		/* Found existing stob domain, kill it. */
		if (rc == 0 && force) {
			rc = m0_stob_domain_destroy(*dom);
			if (rc != 0)
				goto out;
		}
		if (rc != 0 || force) {
			/** @todo allow different stob type for data
			    stobs & ADDB stobs? */
			rc = m0_stob_domain_create_or_init
				(rctx->rc_addb_stlocation, NULL, 0, NULL, dom);
		}
	}
	if (rc != 0)
		return M0_ERR(rc);

	rc = cs_addb_stob_init(rctx, addb_stob, *dom, key);
out:
	if (rc != 0)
		m0_stob_domain_fini(*dom);
	return M0_RC(rc);
}

/**
   Puts the reference of the hard-coded stob, and does the general fini
 */
static void cs_addb_storage_fini(struct cs_addb_stob *addb_stob)
{
	m0_stob_put(addb_stob->cas_stob);
	cs_storage_fini(&addb_stob->cas_stobs);
}

static void be_seg_init(struct m0_be_ut_backend *be,
			m0_bcount_t		 size,
			bool		 	 preallocate,
			bool		 	 format,
			const char		*stob_create_cfg,
			struct m0_be_seg       **out)
{
	struct m0_be_seg *seg;

	seg = m0_be_domain_seg_first(&be->but_dom);
	if (seg != NULL && format) {
		m0_be_ut_backend_seg_del(be, seg);
		seg = NULL;
	}
	if (seg == NULL) {
		if (size == 0)
			size = M0_BE_SEG_SIZE_DEFAULT;
		m0_be_ut_backend_seg_add2(be, size, preallocate,
					  stob_create_cfg, &seg);
	}
	*out = seg;
}

static int cs_be_init(struct m0_reqh_context *rctx,
		      struct m0_be_ut_backend *be,
		      const char              *name,
		      bool                     preallocate,
		      bool                     format,
		      struct m0_be_seg	     **out)
{
	enum { len = 1024 };
	char **loc = &be->but_stob_domain_location;
	int    rc;

	*loc = m0_alloc(len);
	if (*loc == NULL)
		return M0_ERR(-ENOMEM);
	snprintf(*loc, len, "linuxstob:%s%s", name[0] == '/' ? "" : "./", name);

	m0_be_ut_backend_cfg_default(&be->but_dom_cfg);
	be->but_dom_cfg.bc_log.lc_store_cfg.lsc_stob_create_cfg =
		rctx->rc_be_log_path;
	be->but_dom_cfg.bc_seg0_cfg.bsc_stob_create_cfg = rctx->rc_be_seg0_path;
	if (!m0_is_po2(rctx->rc_be_log_size))
		return M0_ERR(-EINVAL);
	if (rctx->rc_be_log_size > 0) {
		be->but_dom_cfg.bc_log.lc_store_cfg.lsc_size =
			rctx->rc_be_log_size;
		be->but_dom_cfg.bc_engine.bec_log_size = rctx->rc_be_log_size;
	}
	rc = m0_be_ut_backend_init_cfg(be, &be->but_dom_cfg, format);
	if (rc != 0)
		goto err;

	be_seg_init(be, rctx->rc_be_seg_size, preallocate, format,
		    rctx->rc_be_seg_path, out);
	if (*out != NULL)
		return 0;
	M0_LOG(M0_ERROR, "cs_be_init: failed to init segment");
	rc = M0_ERR(-ENOMEM);
err:
	m0_free0(loc);
	return M0_ERR(rc);
}

M0_INTERNAL void cs_be_fini(struct m0_be_ut_backend *be)
{
	m0_be_ut_backend_fini(be);
	m0_free(be->but_stob_domain_location);
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
static int cs_reqh_start(struct m0_reqh_context *rctx, bool mkfs, bool force)
{
	/* @todo Have a generic mechanism to generate unique cob domain id.
	   @todo Handle error messages properly */
	static int cdom_id = M0_MDS_COB_ID_START;
	int        rc;

	M0_ENTRY();
	M0_PRE(reqh_context_invariant(rctx));

	rc = M0_REQH_INIT(&rctx->rc_reqh,
			  .rhia_mdstore = &rctx->rc_mdstore,
			  .rhia_pc = &rctx->rc_mero->cc_pools_common,
			  .rhia_fid = &rctx->rc_fid);
	if (rc != 0)
		goto out;

	rctx->rc_be.but_dom_cfg.bc_engine.bec_group_fom_reqh = &rctx->rc_reqh;

	rc = cs_be_init(rctx, &rctx->rc_be, rctx->rc_bepath,
			rctx->rc_be_seg_preallocate,
			(mkfs && force), &rctx->rc_beseg);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "cs_be_init");
		goto reqh_fini;
	}

	rc = m0_reqh_be_init(&rctx->rc_reqh, rctx->rc_beseg);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "m0_reqh_be_init: rc=%d", rc);
		goto be_fini;
	}

	if (rctx->rc_dfilepath != NULL) {
		rc = cs_stob_file_load(rctx->rc_dfilepath, &rctx->rc_stob);
		if (rc != 0) {
			M0_LOG(M0_ERROR,
			       "Failed to load device configuration file");
			goto reqh_be_fini;
		}
	}

	rc = cs_storage_init(rctx->rc_stype, rctx->rc_stpath,
			     M0_AD_STOB_LINUX_DOM_KEY,
			     &rctx->rc_stob, rctx->rc_beseg,
			     mkfs, force);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "cs_storage_init: rc=%d", rc);
		/* XXX who should call yaml_document_delete()? */
		goto reqh_be_fini;
	}

	rc = cs_addb_storage_init(rctx, &rctx->rc_addb_stob,
				  m0_addb_stob_key, mkfs, force);
	if (rc != 0)
		goto cleanup_stob;

	rc = cs_addb_stob_init(rctx, &rctx->rc_addb2_stob,
			       rctx->rc_addb_stob.cas_stobs.s_sdom,
			       m0_addb2_stob_key);
	if (rc != 0)
		goto cleanup_addb_stob;

	rc = m0_reqh_addb2_init(&rctx->rc_reqh, rctx->rc_addb2_stob.cas_stob,
				mkfs);
	if (rc != 0)
		goto cleanup_addb2_stob;

	rctx->rc_cdom_id.id = ++cdom_id;

	/*
	  This MUST be initialized before m0_mdstore_init() is called.
	  Otherwise it returns -ENOENT, which is used for detecting if
	  fs is alive and should be preserved.
	 */
	rctx->rc_mdstore.md_dom = m0_reqh_lockers_get(&rctx->rc_reqh,
						      m0_get()->i_mds_cdom_key);

	if (mkfs) {
		/*
		 * Init mdstore without root cob first. Now we can use it
		 * for mkfs.
		 */
		rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id,
				     rctx->rc_beseg, false);
		if (rc != 0 && rc != -ENOENT) {
			M0_LOG(M0_ERROR, "m0_mdstore_init: rc=%d", rc);
			goto cleanup_addb2;
		}

		/* Prepare new metadata structure, erase old one if exists. */
		if ((rc == 0 && force) || rc == -ENOENT)
			rc = cs_storage_prepare(rctx, (rc == 0 && force));
		m0_mdstore_fini(&rctx->rc_mdstore);
		if (rc != 0) {
			M0_LOG(M0_ERROR, "cs_storage_prepare: rc=%d", rc);
			goto cleanup_addb2;
		}
	}

	M0_ASSERT(rctx->rc_mdstore.md_dom != NULL);
	/* Init mdstore and root cob as it should be created by mkfs. */
	rc = m0_mdstore_init(&rctx->rc_mdstore, &rctx->rc_cdom_id,
			     rctx->rc_beseg, true);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "Failed to initialize mdstore. %s",
		       !mkfs ? "Did you run mkfs?" : "Mkfs failed?");
		goto cleanup_addb2;
	}

	rctx->rc_state = RC_INITIALISED;
	return M0_RC(rc);

cleanup_addb2:
	m0_reqh_addb2_fini(&rctx->rc_reqh);
cleanup_addb2_stob:
	m0_stob_put(rctx->rc_addb2_stob.cas_stob);
cleanup_addb_stob:
	cs_addb_storage_fini(&rctx->rc_addb_stob);
cleanup_stob:
	cs_storage_fini(&rctx->rc_stob);
reqh_be_fini:
	m0_reqh_be_fini(&rctx->rc_reqh);
be_fini:
	cs_be_fini(&rctx->rc_be);
reqh_fini:
	m0_reqh_fini(&rctx->rc_reqh);
out:
	return M0_ERR(rc);
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

	if (m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL)
		m0_reqh_shutdown_wait(reqh);

	if (M0_IN(m0_reqh_state_get(reqh), (M0_REQH_ST_DRAIN, M0_REQH_ST_INIT)))
		m0_reqh_pre_storage_fini_svcs_stop(reqh);

	cs_rpc_machines_fini(reqh);

	M0_ASSERT(m0_reqh_state_get(reqh) == M0_REQH_ST_STOPPED);
	m0_reqh_be_fini(reqh);
	m0_mdstore_fini(&rctx->rc_mdstore);
	m0_reqh_addb2_fini(reqh);
	m0_stob_put(rctx->rc_addb2_stob.cas_stob);
	cs_addb_storage_fini(&rctx->rc_addb_stob);
	cs_storage_fini(&rctx->rc_stob);
	cs_be_fini(&rctx->rc_be);
	m0_reqh_post_storage_fini_svcs_stop(reqh);
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

char *m0_cs_profile_get(struct m0_mero *cctx)
{
	return cctx->cc_profile;
}

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
	cs_eps_tlist_init(&cctx->cc_mds_eps);
	cctx->cc_args.ca_argc = 0;
	cctx->cc_profile = NULL;
	cctx->cc_confd_addr = NULL;
}

static void cs_mero_fini(struct m0_mero *cctx)
{
	struct cs_endpoint_and_xprt *ep;

	m0_free(cctx->cc_confd_addr);
	m0_free(cctx->cc_profile);

	m0_tl_for(cs_eps, &cctx->cc_ios_eps, ep) {
		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		M0_ASSERT(ep->ex_scrbuf != NULL);
		m0_free(ep->ex_scrbuf);
		cs_eps_tlink_del_fini(ep);
		cs_endpoint_and_xprt_bob_fini(ep);
		m0_free(ep);
	} m0_tl_endfor;
	cs_eps_tlist_fini(&cctx->cc_ios_eps);

	m0_tl_for(cs_eps, &cctx->cc_mds_eps, ep) {
		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		M0_ASSERT(ep->ex_scrbuf != NULL);
		m0_free(ep->ex_scrbuf);
		cs_eps_tlink_del_fini(ep);
		cs_endpoint_and_xprt_bob_fini(ep);
		m0_free(ep);
	} m0_tl_endfor;
	cs_eps_tlist_fini(&cctx->cc_mds_eps);

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
"  -Z       Run as a daemon.\n"
"  -R addr  Stats service endpoint address.\n"
"  -F       Force mkfs to override found filesystem.\n"
"  -t num   Timeout value to wait for connection to confd. (default %u sec)\n"
"  -r num   Number of retries in connecting to confd. (default %u)\n"
"  -z num   backend segment size in bytes (used only by m0mkfs).\n"
"\n"
"Request handler options:\n"
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
"  -f fid   FID of the process, in case of m0d is a mandatory parameter\n"
"  -s str   Service (type) to be started.\n"
"           The string is of the following form:\n"
"              ServiceTypeName:ServiceFID\n"
"              e.g. -s 'ioservice:<0x7300000000000001:1>'"
"           Multiple '-s' options are allowed, but the values must be unique.\n"
"           Use '-l' to get a list of registered service types.\n"
"  -q num   [optional] Minimum length of TM receive queue.\n"
"           Defaults to the value set with '-Q' option.\n"
"  -m num   [optional] Maximum RPC message size.\n"
"           Defaults to the value set with '-M' option.\n"
"\n"
"Example:\n"
"    %s -Q 4 -M 4096 -T linux -D bepath -S stobfile \\\n"
"        -e lnet:172.18.50.40@o2ib1:12345:34:1 \\\n"
"        -s 'mds:<0x7300000000000002:1>'-q 8 -m 65536 \\\n"
"        -f '<0x7200000000000001:1>'\n",
CONFD_CONN_TIMEOUT, CONFD_CONN_RETRY, progname);
}

static int reqh_ctx_validate(struct m0_mero *cctx)
{
	struct m0_reqh_context      *rctx = &cctx->cc_reqh_ctx;
	struct cs_endpoint_and_xprt *ep;
	int                          i;
	M0_ENTRY();

	if (!reqh_ctx_args_are_valid(rctx))
		return M0_ERR_INFO(-EINVAL,
				   "Parameters are missing or invalid\n"
				   "Failed condition: %s", m0_failed_condition);

	cctx->cc_recv_queue_min_length = max64(cctx->cc_recv_queue_min_length,
					       M0_NET_TM_RECV_QUEUE_DEF_LEN);
	rctx->rc_recv_queue_min_length = max64(rctx->rc_recv_queue_min_length,
					       M0_NET_TM_RECV_QUEUE_DEF_LEN);

	if (rctx->rc_max_rpc_msg_size == 0)
		rctx->rc_max_rpc_msg_size = cctx->cc_max_rpc_msg_size;

	if (!stype_is_valid(rctx->rc_stype)) {
		cs_stob_types_list(cctx->cc_outfile);
		return M0_ERR_INFO(-EINVAL, "Invalid service type");
	}

	if (cs_eps_tlist_is_empty(&rctx->rc_eps) && rctx->rc_nr_services == 0)
		return M0_RC(0);

	if (cs_eps_tlist_is_empty(&rctx->rc_eps))
		return M0_ERR_INFO(-EINVAL, "Endpoint is missing");

	m0_tl_for(cs_eps, &rctx->rc_eps, ep) {
		int rc;

		M0_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		rc = cs_endpoint_validate(cctx, ep->ex_endpoint, ep->ex_xprt);
		if (rc != 0)
			return M0_ERR_INFO(rc, "Invalid endpoint: %s",
				      ep->ex_endpoint);
	} m0_tl_endfor;

	for (i = 0; i < rctx->rc_nr_services; ++i) {
		const char *sname = rctx->rc_services[i];

		if (!m0_reqh_service_is_registered(sname))
			return M0_ERR_INFO(-ENOENT,
					   "Service is not registered: %s",
					   sname);

		if (service_is_duplicate(rctx, sname))
			return M0_ERR_INFO(-EEXIST, "Service is not unique: %s",
				      sname);
	}

	if (cctx->cc_profile == NULL)
		return M0_ERR_INFO(-EINVAL, "Configuration profile is missing");

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
   Parses a service string of the following form:
   - service-type:fid

   It isolates and parses the fid string, and returns it in the fid parameter.

   @param str Input string
   @param svc Allocated service type name
   @param fid Numerical service fid value, mandatory and valid.
 */
static int
service_string_parse(const char *str, char **svc, struct m0_fid *fid)
{
	const char *colon;
	size_t      len;
	int         rc;

	fid->f_key = fid->f_container = 0;
	colon = strchr(str, ':');
	if (colon == NULL) {
		/* Service FID is mandatory */
		return M0_ERR(-EINVAL);
	}

	/* isolate and copy the service type */
	len = colon - str;
	*svc = m0_alloc(len + 1);
	if (*svc == NULL)
		return M0_ERR(-ENOMEM);
	strncpy(*svc, str, len);
	*(*svc + len) = '\0';

	/* parse the FID */
	rc = m0_fid_sscanf(++colon, fid);
	if (rc != 0)
		m0_free0(svc);
	if (!m0_fid_is_valid(fid) ||
	    !M0_CONF_SERVICE_TYPE.cot_ftype.ft_is_valid(fid))
		return M0_ERR(-EINVAL);
	return M0_RC(rc);
}

static int process_fid_parse(const char *str, struct m0_fid *fid)
{
	M0_PRE(str != NULL);
	M0_PRE(fid != NULL);
	return m0_fid_sscanf(str, fid) ?:
	       m0_fid_tget(fid) == M0_CONF_PROCESS_TYPE.cot_ftype.ft_id &&
	       m0_fid_is_valid(fid) ?
	       0 : M0_ERR(-EINVAL);
}

/* With this, utilities like m0mkfs will generate process FID on the fly */
static void process_fid_generate_conditional(struct m0_reqh_context *rctx)
{
	if (!m0_fid_is_set(&rctx->rc_fid)) {
		m0_uuid_generate((struct m0_uint128*)&rctx->rc_fid);
		m0_fid_tassume(&rctx->rc_fid, &M0_CONF_PROCESS_TYPE.cot_ftype);
	}
}

/** Parses CLI arguments, filling m0_mero structure. */
static int _args_parse(struct m0_mero *cctx, int argc, char **argv)
{
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;
	int                     rc_getops;
	int                     rc = 0;

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
			M0_VOIDARG('F', "Force mkfs to override found filesystem",
				LAMBDA(void, (void)
				{
					cctx->cc_force = true;
				})),
			M0_FORMATARG('Q', "Minimum TM Receive queue length",
				     "%i", &cctx->cc_recv_queue_min_length),
			M0_FORMATARG('M', "Maximum RPC message size", "%i",
				     &cctx->cc_max_rpc_msg_size),
			M0_STRINGARG('C', "Confd endpoint address",
				LAMBDA(void, (const char *s)
				{
					cctx->cc_confd_addr = m0_strdup(s);
					M0_ASSERT(cctx->cc_confd_addr != NULL);
				})),
			M0_STRINGARG('P', "Configuration profile",
				LAMBDA(void, (const char *s)
				{
					cctx->cc_profile = m0_strdup(s);
					M0_ASSERT(cctx->cc_profile != NULL);
				})),
			M0_STRINGARG('G', "Mdservice endpoint address",
				LAMBDA(void, (const char *s)
				{
					rc = ep_and_xprt_append(
						&cctx->cc_mds_eps, s);
					M0_LOG(M0_DEBUG, "adding %s to md ep "
					       "list %d", s, rc);
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
			M0_NUMBERARG('r', "confd retry",
				LAMBDA(void, (int64_t val)
				{
					cctx->cc_confd_conn_retry = val;
				})),
			M0_NUMBERARG('t', "confd timeout",
				LAMBDA(void, (int64_t val)
				{
					cctx->cc_confd_timeout = val;
				})),
			M0_FLAGARG('Z', "Run as a daemon", &cctx->cc_daemon),

			/* -------------------------------------------
			 * Request handler options
			 */
			M0_STRINGARG('D', "Metadata storage filename",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_bepath = s;
				})),
			M0_STRINGARG('L', "BE log file path",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_be_log_path = s;
				})),
			M0_STRINGARG('b', "BE seg0 file path",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_be_seg0_path = s;
				})),
			M0_STRINGARG('B', "BE primary segment file path",
				LAMBDA(void, (const char *s)
				{
					rctx->rc_be_seg_path = s;
				})),
			M0_NUMBERARG('z', "BE primary segment size",
				LAMBDA(void, (int64_t size)
				{
					rctx->rc_be_seg_size = size;
				})),
			M0_NUMBERARG('V', "BE log size",
				LAMBDA(void, (int64_t size)
				{
					rctx->rc_be_log_size = size;
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
		       M0_STRINGARG('f', "Process FID string",
				LAMBDA(void, (const char *s)
				{
				      rc = process_fid_parse(s, &rctx->rc_fid);
				})),
			M0_STRINGARG('s', "Services to be configured",
				LAMBDA(void, (const char *s)
				{
					int i;
					if (rctx->rc_nr_services >=
					    rctx->rc_max_services) {
						rc = M0_ERR(-E2BIG);
						M0_LOG(M0_ERROR,
						       "Too many services");
						return;
					}
					i = rctx->rc_nr_services;
					rc = service_string_parse(s,
						   &rctx->rc_services[i],
						   &rctx->rc_service_fids[i]);
					if (rc == 0)
						M0_CNT_INC(
							rctx->rc_nr_services);
				})),
			M0_FLAGARG('a', "Preallocate BE seg",
				   &rctx->rc_be_seg_preallocate),
			M0_VOIDARG('v', "Print version and exit",
				LAMBDA(void, (void)
				{
					m0_build_info_print();
					rc = 1;
				})),
			M0_STRINGARG('u', "Node UUID",
				LAMBDA(void, (const char *s)
				{
					/* not used here, it's a placeholder */
				})),
			);
	/* generate reqh fid in case it is all-zero */
	process_fid_generate_conditional(rctx);

	return M0_RC(rc_getops ?: rc);
}

static int cs_args_parse(struct m0_mero *cctx, int argc, char **argv)
{
	M0_ENTRY();
	return _args_parse(cctx, argc, argv);
}

M0_INTERNAL const char *m0_cs_local_ep(struct m0_mero *cctx)
{
	struct cs_endpoint_and_xprt *epx;

	epx = cs_eps_tlist_head(&cctx->cc_reqh_ctx.rc_eps);
	return epx->ex_endpoint;
}

static int file_read(const char *path, char **dest)
{
	FILE  *f;
	long   size;
	size_t n;
	int    rc = 0;

	M0_ENTRY("path=`%s'", path);

	f = fopen(path, "rb");
	if (f == NULL)
		return M0_ERR_INFO(-errno, "path=`%s'", path);

	rc = fseek(f, 0, SEEK_END);
	if (rc == 0) {
		size = ftell(f);
		rc = fseek(f, 0, SEEK_SET);
	}
	if (rc != 0) {
		fclose(f);
		return M0_ERR_INFO(-errno, "fseek() failed: path=`%s'", path);
	}
	/* it should be freed by the caller */
	M0_ALLOC_ARR(*dest, size + 1);
	if (*dest != NULL) {
		n = fread(*dest, 1, size + 1, f);
		M0_ASSERT_INFO(n == size, "n=%zu size=%ld", n, size);
		if (ferror(f))
			rc = -errno;
		else if (!feof(f))
			rc = -EFBIG;
		else
			(*dest)[n] = '\0';
	} else {
		rc = -ENOMEM;
	}

	fclose(f);
	return M0_RC(rc);
}

static int cs_conf_setup(struct m0_mero *cctx)
{
	struct cs_args       *args = &cctx->cc_args;
	struct m0_confc_args *conf_args;
	char                 *confstr = NULL;
	struct m0_conf_filesystem *fs;
	int                   rc;

	if (cctx->cc_reqh_ctx.rc_confdb != NULL) {
		rc = file_read(cctx->cc_reqh_ctx.rc_confdb, &confstr);
		if (rc != 0)
			return M0_ERR(rc);
	}

	conf_args = &(struct m0_confc_args){
		.ca_profile = cctx->cc_profile,
		.ca_confstr = confstr,
		.ca_confd   = cctx->cc_confd_addr,
		.ca_rmach   = m0_mero_to_rmach(cctx),
		.ca_group   = m0_locality0_get()->lo_grp,
	};

	rc = m0_reqh_conf_setup(&cctx->cc_reqh_ctx.rc_reqh, conf_args);
	if (rc != 0) {
		m0_free(confstr);
		return M0_ERR(rc);
	}

	rc = m0_conf_fs_get(conf_args->ca_profile, m0_mero2confc(cctx), &fs);
	if (rc != 0)
		goto conf_destroy;

	rc = m0_conf_full_load(fs);
	if (rc != 0)
		goto conf_fs_close;

	rc = cs_conf_to_args(args, fs) ?:
		_args_parse(cctx, args->ca_argc, args->ca_argv);
	if (rc != 0)
		goto conf_fs_close;

	m0_pools_common_init(&cctx->cc_pools_common,
			     m0_mero_to_rmach(cctx), fs);

	rc = m0_pools_setup(&cctx->cc_pools_common, fs, NULL, NULL, NULL);
	if (rc != 0)
		goto pools_common_fini;
	if (cctx->cc_pools_common.pc_md_redundancy > 0)
		cctx->cc_reqh_ctx.rc_reqh.rh_oostore = true;

	m0_confc_close(&fs->cf_obj);
	m0_free(confstr);
	return M0_RC(rc);

pools_common_fini:
	m0_pools_common_fini(&cctx->cc_pools_common);
conf_fs_close:
	m0_confc_close(&fs->cf_obj);
conf_destroy:
	m0_confc_fini(m0_mero2confc(cctx));
	m0_free(confstr);
	return M0_ERR(rc);
}

static void cs_conf_destroy(struct m0_mero *cctx)
{
	struct m0_confc *confc = m0_mero2confc(cctx);

	if (confc->cc_group != NULL) {
		m0_pool_versions_destroy(&cctx->cc_pools_common);
		m0_pools_service_ctx_destroy(&cctx->cc_pools_common);
		m0_pools_destroy(&cctx->cc_pools_common);
		m0_pools_common_fini(&cctx->cc_pools_common);
		m0_confc_fini(m0_mero2confc(cctx));
	}
}

static int cs_reqh_layouts_setup(struct m0_mero *cctx)
{
	if (cctx->cc_profile == NULL && cctx->cc_confd_addr == NULL)
		return 0;
	int rc = m0_reqh_layouts_setup(&cctx->cc_reqh_ctx.rc_reqh,
				       &cctx->cc_pools_common);
	return M0_RC(rc);
}

int m0_cs_setup_env(struct m0_mero *cctx, int argc, char **argv)
{
	int rc;

	if (M0_FI_ENABLED("fake_error"))
		return M0_ERR(-EINVAL);

	m0_rwlock_write_lock(&cctx->cc_rwlock);
	rc = cs_args_parse(cctx, argc, argv) ?:
	     reqh_ctx_validate(cctx) ?:
	     cs_daemonize(cctx) ?:
	     cs_net_domains_init(cctx) ?:
	     cs_buffer_pool_setup(cctx) ?:
	     cs_reqh_start(&cctx->cc_reqh_ctx, cctx->cc_mkfs, cctx->cc_force) ?:
	     cs_rpc_machines_init(cctx) ?:
	     cs_conf_setup(cctx);
	m0_rwlock_write_unlock(&cctx->cc_rwlock);
	return M0_RC(rc);
}

int m0_cs_start(struct m0_mero *cctx)
{
	struct m0_reqh_context     *rctx = &cctx->cc_reqh_ctx;
	struct m0_reqh             *reqh = &cctx->cc_reqh_ctx.rc_reqh;
	int                         rc;
	struct m0_conf_filesystem  *fs;

	M0_ENTRY();
	M0_PRE(reqh_context_invariant(rctx));

	rc = reqh_services_start(rctx);
	if (rc != 0)
		return M0_ERR(rc);

        rc = m0_conf_fs_get(cctx->cc_profile, m0_mero2confc(cctx), &fs);
        if (rc != 0)
		return M0_ERR(rc);

	rc = m0_pools_service_ctx_create(&cctx->cc_pools_common, fs);
	if (rc != 0)
		goto error;

	rc = m0_pool_versions_setup(&cctx->cc_pools_common, fs,
				    NULL, NULL, NULL);
        if (rc != 0)
		goto error;

	rc = m0_reqh_ha_setup(reqh);
        if (rc != 0)
		goto error;

        rc = m0_conf_failure_sets_build(&reqh->rh_pools->pc_ha_ctx->sc_session,
					fs, &reqh->rh_failure_sets);
	if (rc != 0)
                return M0_ERR(rc);

	rc = cs_reqh_layouts_setup(cctx);
        if (rc != 0)
		return M0_ERR(rc);

	m0_confc_close(&fs->cf_obj);
	return M0_RC(rc);

error:
	m0_confc_close(&fs->cf_obj);
	return M0_ERR(rc);
}

int m0_cs_init(struct m0_mero *cctx, struct m0_net_xprt **xprts,
	       size_t xprts_nr, FILE *out, bool mkfs)
{
	M0_PRE(xprts != NULL && xprts_nr > 0 && out != NULL);

	if (M0_FI_ENABLED("fake_error"))
		return M0_ERR(-EINVAL);

	cctx->cc_xprts    = xprts;
	cctx->cc_xprts_nr = xprts_nr;
	cctx->cc_outfile  = out;
	cctx->cc_mkfs     = mkfs;
	cctx->cc_force    = false;

	cs_mero_init(cctx);

	return cs_reqh_ctx_init(cctx);
}

void m0_cs_fini(struct m0_mero *cctx)
{
	struct m0_reqh_context *rctx = &cctx->cc_reqh_ctx;
	struct m0_reqh         *reqh = &cctx->cc_reqh_ctx.rc_reqh;

	M0_ENTRY();

	if (rctx->rc_state == RC_INITIALISED)
		m0_reqh_layouts_cleanup(reqh);

	if (cctx->cc_pools_common.pc_ha_ctx != NULL)
		m0_conf_failure_sets_destroy(&reqh->rh_failure_sets);

	cs_conf_destroy(cctx);
	if (rctx->rc_state == RC_INITIALISED)
		cs_reqh_stop(rctx);
	cs_reqh_ctx_fini(rctx);

	cs_buffer_pool_fini(cctx);
	cs_net_domains_fini(cctx);
	cs_mero_fini(cctx);

	M0_LEAVE();
}

/**
 * Extract the path of the provided dev_id from the config file, create stob id
 * for it and call m0_stob_linux_reopen() to reopen the stob.
 */
M0_INTERNAL int m0_mero_stob_reopen(struct m0_reqh *reqh,
				    uint32_t dev_id)
{
	struct m0_stob_id       stob_id;
	struct m0_reqh_context *reqh_ctx;
	struct cs_stobs        *stob;
	yaml_document_t        *doc;
	yaml_node_t            *node;
	yaml_node_t            *s_node;
	yaml_node_item_t       *item;
	const char             *f_path;
	uint64_t                cid;
	int                     rc = 0;
	int                     result;

	reqh_ctx = container_of(reqh, struct m0_reqh_context, rc_reqh);
	stob = &reqh_ctx->rc_stob;
	doc = &stob->s_sfile.sf_document;
	if (!stob->s_sfile.sf_is_initialised) {
		return M0_ERR(-EINVAL);
	}
	for (node = doc->nodes.start; node < doc->nodes.top; ++node) {
		for (item = (node)->data.sequence.items.start;
		     item < (node)->data.sequence.items.top; ++item) {
			s_node = yaml_document_get_node(doc, *item);
			result = stob_file_id_get(doc, s_node, &cid);
			if (result != 0)
				continue;
			if (cid == dev_id) {
				f_path = stob_file_path_get(doc, s_node);
				m0_stob_id_make(0, cid, &stob->s_sdom->sd_id,
						&stob_id);
				rc = m0_stob_linux_reopen(&stob_id, f_path);
				if (rc != 0)
					return M0_ERR(rc);
			}
		}
	}
	return M0_RC(rc);
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
