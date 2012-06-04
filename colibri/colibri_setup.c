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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>     /* fprintf */
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <string.h>    /* strtok_r, strcmp */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/getopts.h"
#include "lib/processor.h"
#include "lib/time.h"
#include "lib/misc.h"
#include "lib/finject.h"    /* C2_FI_ENABLED */

#include "balloc/balloc.h"
#include "stob/ad.h"
#include "stob/linux.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc2.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "colibri/colibri_setup.h"
#include "rpc/rpclib.h"

/**
   @addtogroup colibri_setup
   @{
 */

/**
   Represents cob domain id, it is incremented for every new cob domain.

   @todo Have a generic mechanism to generate unique cob domain id.
 */
static int cdom_id;

/**
   Magic used to check consistency of cs_reqh_context.
 */
enum {
	CS_REQH_CTX_MAGIX = 0x5248435458, /* RHCTX */
	CS_REQH_CTX_HEAD_MAGIX = 0x52484354584844, /* RHCTXHD */
	CS_NET_DOMS_HEAD_MAGIX = 0x4E444F4D4844, /* NDOMHD */
	CS_ENDPOINT_MAGIX = 0x43535F4550, /* CS_EP */
	CS_ENDPOINT_HEAD_MAGIX = 0x43535F45504844 /* CS_EPHD */
};

enum {
	WAIT_FOR_REQH_SHUTDOWN = 1000000,
	CS_OPTLENGTH = 2
};

enum {
	CS_MAX_EP_ADDR_LEN = 80,
};
C2_BASSERT(CS_MAX_EP_ADDR_LEN >= C2_NET_LNET_XEP_ADDR_LEN);

C2_TL_DESCR_DEFINE(cs_buffer_pools, "buffer pools in the colibri context", ,
                   struct c2_cs_buffer_pool, cs_bp_linkage, cs_bp_magic,
                   C2_CS_BUFFER_POOL_MAGIC, C2_CS_BUFFER_POOL_HEAD);
C2_TL_DEFINE(cs_buffer_pools, , struct c2_cs_buffer_pool);

extern const struct c2_tl_descr c2_rstypes_descr;
extern struct c2_tl		c2_rstypes;
extern struct c2_mutex		c2_rstypes_mutex;

/*
 * Name of cobfid_map table which stores the auxiliary database records.
 * This symbol has to be exposed to UTs and possibly other modules in future.
 */
static const char cobfid_map_name[] = "cobfid_map";

/**
   Represents state of a request handler context.
 */
enum {
        /**
	   A request handler context is in RC_UNINTIALISED state when it is
	   allocated and added to the list of the same in struct c2_colibri.

	   @see c2_colibri::cc_reqh_ctxs
	 */
	RC_UNINITIALISED,
	/**
	   A request handler context is in RC_INITIALISED state once the
	   request handler (embedded inside the context) is successfully
	   initialised.

	   @see cs_reqh_context::rc_reqh
	 */
	RC_INITIALISED
};

/**
   Contains extracted network endpoint and transport from colibri endpoint.
 */
struct cs_endpoint_and_xprt {
	/**
	   4-tuple network layer endpoint address.
	   e.g. 172.18.50.40@o2ib1:12345:34:1
	 */
	const char      *ex_endpoint;
	/** Supported network transport. */
	const char      *ex_xprt;
	/**
	   Scratch buffer for endpoint and transport extraction.
	 */
	char            *ex_scrbuf;
	uint64_t         ex_magic;
	/** Linkage into reqh context endpoint list, cs_reqh_context::rc_eps */
	struct c2_tlink  ex_linkage;
	/**
	   Unique Colour to be assigned to each TM.
	   @see c2_net_transfer_mc::ntm_pool_colour.
	 */
	uint32_t	 ex_tm_colour;
};

C2_TL_DESCR_DEFINE(cs_eps, "cs endpoints", static, struct cs_endpoint_and_xprt,
                   ex_linkage, ex_magic, CS_ENDPOINT_MAGIX,
		   CS_ENDPOINT_HEAD_MAGIX);

C2_TL_DEFINE(cs_eps, static, struct cs_endpoint_and_xprt);

static struct c2_bob_type cs_eps_bob;
C2_BOB_DEFINE(static, &cs_eps_bob, cs_endpoint_and_xprt);

/**
   Represents a request handler environment.
   It contains configuration information about the various global entities
   to be configured and their corresponding instances that are needed to be
   initialised before the request handler is started, which by itself is
   contained in the same structure.
 */
struct cs_reqh_context {
	/** Storage path for request handler context. */
	const char                  *rc_stpath;

	/** Type of storage to be initialised. */
	const char                  *rc_stype;

	/** Database environment path for request handler context. */
	const char                  *rc_dbpath;

	/** Services running in request handler context. */
	const char                 **rc_services;

	/** Number services in request handler context. */
	int                          rc_snr;

	/**
	    Maximum number of services allowed per request handler context.
	 */
	int                          rc_max_services;

	/** Endpoints and xprts per request handler context. */
	struct c2_tl                 rc_eps;

	/**
	    State of a request handler context, i.e. RC_INITIALISED or
	    RC_UNINTIALISED.
	 */
	int                          rc_state;

	/** Storage domain for a request handler */
	struct c2_cs_reqh_stobs      rc_stob;

	/** Database used by the request handler */
	struct c2_dbenv              rc_db;

	/** Cob domain to be used by the request handler */
	struct c2_cob_domain         rc_cdom;

	struct c2_cob_domain_id      rc_cdom_id;

	/** File operation log for a request handler */
	struct c2_fol                rc_fol;

	/** Request handler instance to be initialised */
	struct c2_reqh               rc_reqh;

	/** Reqh context magic */
	uint64_t                     rc_magic;

	/** Linkage into reqh context list */
	struct c2_tlink              rc_linkage;

	/** Backlink to struct c2_colibri. */
	struct c2_colibri	    *rc_colibri;

	/**
	 * Minimum number of buffers in TM receive queue.
	 * Default is set to c2_colibri::cc_recv_queue_min_length
	 */
	uint32_t		     rc_recv_queue_min_length;

	/**
	 * Maximum RPC message size.
	 * Default value is set to c2_colibri::cc_max_rpc_msg_size
	 * If value of cc_max_rpc_msg_size is zero then value from
	 * c2_net_domain_get_max_buffer_size() is used.
	 */
	uint32_t		     rc_max_rpc_msg_size;
};

enum {
	LINUX_STOB,
	AD_STOB,
	STOBS_NR
};

/**
   Currently supported stob types in colibri context.
 */
static const char *cs_stobs[] = {
	[LINUX_STOB] = "Linux",
	[AD_STOB]    = "AD"
};

C2_TL_DESCR_DEFINE(rhctx, "reqh contexts", static, struct cs_reqh_context,
                   rc_linkage, rc_magic, CS_REQH_CTX_MAGIX,
		   CS_REQH_CTX_HEAD_MAGIX);

C2_TL_DEFINE(rhctx, static, struct cs_reqh_context);

static struct c2_bob_type rhctx_bob;
C2_BOB_DEFINE(static, &rhctx_bob, cs_reqh_context);

C2_TL_DESCR_DEFINE(ndom, "network domains", static, struct c2_net_domain,
                   nd_app_linkage, nd_magic, C2_NET_DOMAIN_MAGIX,
		   CS_NET_DOMS_HEAD_MAGIX);

C2_TL_DEFINE(ndom, static, struct c2_net_domain);

static struct c2_bob_type ndom_bob;
C2_BOB_DEFINE(static, &ndom_bob, c2_net_domain);

static struct c2_net_domain *cs_net_domain_locate(struct c2_colibri *cctx,
						  const char *xprt);

static int cobfid_map_setup_init(struct c2_colibri *cc, const char *name);

static void cobfid_map_setup_fini(struct c2_ref *ref);

static uint32_t cs_domain_tms_nr(struct c2_colibri *cctx,
				 struct c2_net_domain *dom);

static uint32_t cs_dom_tm_min_recv_queue_total(struct c2_colibri *cctx,
					       struct c2_net_domain *dom);

static void cs_buffer_pool_fini(struct c2_colibri *cctx);
/**
   Looks up an xprt by the name.

   @param xprt_name Network transport name
   @param xprts Array of network transports supported in a colibri environment
   @param xprts_nr Size of xprts array

   @pre xprt_name != NULL && xprts != NULL && xprts_nr > 0

 */
static struct c2_net_xprt *cs_xprt_lookup(const char *xprt_name,
					  struct c2_net_xprt **xprts,
					  int xprts_nr)
{
        int i;

	C2_PRE(xprt_name != NULL && xprts != NULL && xprts_nr > 0);

        for (i = 0; i < xprts_nr; ++i)
                if (strcmp(xprt_name, xprts[i]->nx_name) == 0)
                        return xprts[i];
        return NULL;
}

/**
   Lists supported network transports.
 */
static void cs_xprts_list(FILE *out, struct c2_net_xprt **xprts, int xprts_nr)
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
		fprintf(out, " %s\n", cs_stobs[i]);
}

/**
   Checks if the specified storage type is supported in a colibri context.

   @param stype Storage type

   @pre stype != NULL
 */
static bool stype_is_valid(const char *stype)
{
	C2_PRE(stype != NULL);

	return  strcasecmp(stype, cs_stobs[AD_STOB]) == 0 ||
		strcasecmp(stype, cs_stobs[LINUX_STOB]) == 0;
}

/**
   Lists supported services.
 */
static void cs_services_list(FILE *out)
{
	struct c2_reqh_service_type *stype;

	C2_PRE(out != NULL);

	if (c2_rstypes_tlist_is_empty(&c2_rstypes)) {
		fprintf(out, "No available services\n");
		return;
	}

	fprintf(out, "Supported services:\n");
	c2_tlist_for(&c2_rstypes_tl, &c2_rstypes, stype) {
		C2_ASSERT(c2_reqh_service_type_bob_check(stype));
		fprintf(out, " %s\n", stype->rst_name);
	} c2_tlist_endfor;
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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

        C2_ASSERT(!rhctx_tlist_is_empty(&cctx->cc_reqh_ctxs));

	cnt = 0;
	c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		c2_tlist_for(&cs_eps_tl, &rctx->rc_eps, ep_xprt) {
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
		} c2_tlist_endfor;
	} c2_tlist_endfor;

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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	rc = 0;
	xprt = cs_xprt_lookup(xprt_name, cctx->cc_xprts, cctx->cc_xprts_nr);
	if (xprt == NULL)
		rc = -EINVAL;

	if (strcmp(xprt_name, "lnet") == 0)
		rc = c2_net_lnet_ep_addr_net_cmp(ep, ep);

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
	char                        *sptr;
	struct cs_endpoint_and_xprt *epx;
	char			    *endpoint;

	C2_PRE(ep != NULL);

	C2_ALLOC_PTR(epx);
	C2_ALLOC_ARR(epx->ex_scrbuf, min32u(strlen(ep) + 1, CS_MAX_EP_ADDR_LEN));
	strcpy(epx->ex_scrbuf, ep);
	epx->ex_xprt = strtok_r(epx->ex_scrbuf, ":", &sptr);
	if (epx->ex_xprt == NULL)
		rc = -EINVAL;
	else {
		endpoint = strtok_r(NULL , "\0", &sptr);
		if (endpoint == NULL)
			rc = -EINVAL;
		else {
			epx->ex_endpoint = endpoint;
			cs_eps_tlink_init(epx);
			cs_endpoint_and_xprt_bob_init(epx);
			cs_eps_tlist_add_tail(&rctx->rc_eps, epx);
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
static bool service_is_duplicate(const struct c2_colibri *cctx,
				 const char *sname)
{
	int                     idx;
	int                     cnt;
        struct cs_reqh_context *rctx;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
                for (idx = 0, cnt = 0; idx < rctx->rc_snr; ++idx) {
                        if (strcasecmp(rctx->rc_services[idx], sname) == 0)
                                ++cnt;
                        if (cnt > 1)
                                return true;
                }
        } c2_tlist_endfor;

	return false;
}

static bool service_is_registered(const char *service_name)
{
	struct c2_reqh_service_type *stype;

	c2_tlist_for(&c2_rstypes_tl, &c2_rstypes, stype) {
		C2_ASSERT(c2_reqh_service_type_bob_check(stype));
		if (strcasecmp(stype->rst_name, service_name) == 0)
			return true;
	} c2_tlist_endfor;

	return false;
}

struct c2_rpc_machine *c2_cs_rpc_mach_get(struct c2_colibri *cctx,
					  const struct c2_net_xprt *xprt,
					  const char *sname)
{
	struct c2_reqh         *reqh;
	struct cs_reqh_context *rctx;
	struct c2_reqh_service *service;
	struct c2_rpc_machine  *rpcmach;
	struct c2_net_xprt     *nxprt;

        C2_PRE(cctx != NULL);

	c2_mutex_lock(&cctx->cc_mutex);
        C2_ASSERT(!rhctx_tlist_is_empty(&cctx->cc_reqh_ctxs));

        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		reqh = &rctx->rc_reqh;
                c2_tlist_for(&c2_rhsvc_tl, &reqh->rh_services, service) {
			C2_ASSERT(c2_reqh_service_bob_check(service));
			if (strcmp(service->rs_type->rst_name, sname) != 0)
				continue;
			c2_tlist_for(&c2_rhrpm_tl, &reqh->rh_rpc_machines,
                                                                      rpcmach) {
				C2_ASSERT(c2_rpc_machine_bob_check(rpcmach));
				nxprt = rpcmach->rm_tm.ntm_dom->nd_xprt;
				C2_ASSERT(nxprt != NULL);
				if (nxprt->nx_name == xprt->nx_name) {
					c2_mutex_unlock(&cctx->cc_mutex);
					return rpcmach;
				}
			} c2_tlist_endfor;
                } c2_tlist_endfor;
        } c2_tlist_endfor;
	c2_mutex_unlock(&cctx->cc_mutex);

        return NULL;
}
C2_EXPORTED(c2_cs_rpc_mach_get);

struct c2_net_transfer_mc *c2_cs_tm_get(struct c2_colibri *cctx,
					const struct c2_net_xprt *xprt,
					const char *sname)
{
	struct c2_rpc_machine *rpcmach;

	rpcmach = c2_cs_rpc_mach_get(cctx, xprt, sname);

	return (rpcmach == NULL) ? NULL : &rpcmach->rm_tm;
}

/**
   Checks consistency of request handler context.
 */
static bool cs_reqh_context_invariant(const struct cs_reqh_context *rctx)
{
	if (rctx == NULL)
		return false;
	switch (rctx->rc_state) {
	case RC_UNINITIALISED:
		return rctx->rc_stype != NULL && rctx->rc_dbpath != NULL &&
			rctx->rc_stpath != NULL && rctx->rc_snr != 0;
	case RC_INITIALISED:
		return cs_reqh_context_bob_check(rctx) &&
			c2_reqh_invariant(&rctx->rc_reqh);
	default:
		return false;
	}
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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	C2_ALLOC_PTR(rctx);
	if (rctx == NULL)
		goto out;

	rctx->rc_max_services = c2_rstypes_tlist_length(&c2_rstypes);
	if (rctx->rc_max_services == 0) {
		fprintf(cctx->cc_outfile, "No available services\n");
		goto cleanup_rctx;
	}

	C2_ALLOC_ARR(rctx->rc_services, rctx->rc_max_services);
	if (rctx->rc_services == NULL)
		goto cleanup_rctx;

	rhctx_tlink_init(rctx);
	cs_reqh_context_bob_init(rctx);
	cs_eps_tlist_init(&rctx->rc_eps);
	c2_bob_type_tlist_init(&cs_eps_bob, &cs_eps_tl);
	rhctx_tlist_add_tail(&cctx->cc_reqh_ctxs, rctx);
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

	c2_tlist_for(&cs_eps_tl, &rctx->rc_eps, ep) {
		C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
		C2_ASSERT(ep->ex_scrbuf != NULL);
		c2_free(ep->ex_scrbuf);
		cs_eps_tlist_del(ep);
		cs_endpoint_and_xprt_bob_fini(ep);
		cs_eps_tlink_fini(ep);
		c2_free(ep);
	} c2_tlist_endfor;

	cs_eps_tlist_fini(&rctx->rc_eps);
	c2_free(rctx->rc_services);
	rhctx_tlist_del(rctx);
	cs_reqh_context_bob_fini(rctx);
	rhctx_tlink_fini(rctx);
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
	struct c2_net_domain *ndom = NULL;

	C2_PRE(cctx != NULL && xprt_name != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	c2_tlist_for(&ndom_tl, &cctx->cc_ndoms, ndom) {
		C2_ASSERT(c2_net_domain_bob_check(ndom));
		if (strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0)
			break;
	} c2_tlist_endfor;

	return ndom;
}

static struct c2_net_buffer_pool *cs_buffer_pool_get(struct c2_colibri *cctx,
						     struct c2_net_domain *ndom)
{
	struct c2_cs_buffer_pool *cs_bp;

	C2_PRE(cctx != NULL);
	C2_PRE(ndom != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	ndom = cs_net_domain_locate(cctx, xprt_name);
	if (ndom == NULL)
		return -EINVAL;

	C2_ALLOC_PTR(rpcmach);
	if (rpcmach == NULL)
		return -ENOMEM;

	if (max_rpc_msg_size > c2_net_domain_get_max_buffer_size(ndom))
		return -EINVAL;

	buffer_pool = cs_buffer_pool_get(cctx, ndom);
	c2_rpc_machine_pre_init(rpcmach, ndom, tm_colour, max_rpc_msg_size,
				recv_queue_min_length);

	rc = c2_rpc_machine_init(rpcmach, reqh->rh_cob_domain, ndom, ep, reqh,
				 buffer_pool);
	if (rc != 0) {
		c2_free(rpcmach);
		return rc;
	}

	c2_rhrpm_tlink_init(rpcmach);
	c2_rpc_machine_bob_init(rpcmach);
	c2_rhrpm_tlist_add_tail(&reqh->rh_rpc_machines, rpcmach);

	return rc;
}

/**
   Intialises rpc machines in a colibri context.

   @param cctx Colibri context
 */
static int cs_rpc_machines_init(struct c2_colibri *cctx)
{
	int                          rc;
	FILE                        *ofd;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));
        C2_ASSERT(!rhctx_tlist_is_empty(&cctx->cc_reqh_ctxs));

	ofd = cctx->cc_outfile;
        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		C2_ASSERT(cs_reqh_context_invariant(rctx));

		c2_tlist_for(&cs_eps_tl, &rctx->rc_eps, ep) {
			C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_rpc_machine_init(cctx, ep->ex_xprt,
						 ep->ex_endpoint,
						 ep->ex_tm_colour,
						 rctx->rc_recv_queue_min_length,
						 rctx->rc_max_rpc_msg_size,
						 &rctx->rc_reqh);
			if (rc != 0) {
				fprintf(ofd,
					"COLIBRI: Invalid endpoint: %s:%s\n",
					ep->ex_xprt, ep->ex_endpoint);
				return rc;
			}
		} c2_tlist_endfor;
	} c2_tlist_endfor;

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

	c2_tlist_for(&c2_rhrpm_tl, &reqh->rh_rpc_machines, rpcmach) {
		C2_ASSERT(rpcmach != NULL && c2_rpc_machine_bob_check(rpcmach));
		c2_rhrpm_tlist_del(rpcmach);
		c2_rpc_machine_bob_fini(rpcmach);
		c2_rhrpm_tlink_fini(rpcmach);
		c2_rpc_machine_fini(rpcmach);
		c2_free(rpcmach);
	} c2_tlist_endfor;
}

static int cs_buffer_pool_setup(struct c2_colibri *cctx)
{
	int		          rc = 0;
	struct c2_net_domain     *dom;
	struct c2_cs_buffer_pool *cs_bp;
	uint32_t		  tms_nr;
	uint32_t		  bufs_nr;
	uint32_t                  max_recv_queue_len;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

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

static void cs_buffer_pool_fini(struct c2_colibri *cctx)
{
	struct c2_cs_buffer_pool   *cs_bp;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	c2_tl_for(cs_buffer_pools, &cctx->cc_buffer_pools, cs_bp) {
                cs_buffer_pools_tlink_del_fini(cs_bp);
		c2_net_buffer_pool_fini(&cs_bp->cs_buffer_pool);
		c2_free(cs_bp);
	} c2_tl_endfor;
}

/**
   Initialises AD type stob.
 */
static int cs_ad_stob_init(const char *stob_path, struct c2_cs_reqh_stobs *stob,
                           struct c2_dbenv *db)
{
        int               rc;
	struct c2_dtx    *tx;
	struct c2_balloc *cb;

        C2_PRE(stob != NULL && stob->rs_ldom != NULL);

        tx = &stob->rs_tx;
        stob->rs_id_back.si_bits = (struct c2_uint128){ .u_hi = 0x0,
                                                        .u_lo = 0xadf11e };

        rc = c2_stob_domain_locate(&c2_ad_stob_type, stob_path, &stob->rs_adom);
	if (rc == 0)
             rc = c2_stob_find(stob->rs_ldom, &stob->rs_id_back,
				&stob->rs_stob_back);
        if (rc == 0) {
             c2_dtx_init(tx);
             rc = c2_dtx_open(tx, db);
        }
        if (rc == 0) {
             rc = c2_stob_locate(stob->rs_stob_back, tx);
             if (rc == -ENOENT)
                       rc = c2_stob_create(stob->rs_stob_back, tx);
        }
        if (rc == 0) {
             rc = c2_balloc_locate(&cb);
             if (rc == 0)
                  rc = c2_ad_stob_setup(stob->rs_adom, db,
                                        stob->rs_stob_back,
                                        &cb->cb_ballroom,
                                        BALLOC_DEF_CONTAINER_SIZE,
                                        BALLOC_DEF_BLOCK_SHIFT,
                                        BALLOC_DEF_BLOCKS_PER_GROUP,
                                        BALLOC_DEF_RESERVED_GROUPS);
	}

       return rc;
}

/**
   Initialises linux type stob.
 */
static int cs_linux_stob_init(const char *stob_path,
			      struct c2_cs_reqh_stobs *stob)
{
	int                    rc;
	struct c2_stob_domain *sdom;

	rc = c2_stob_domain_locate(&c2_linux_stob_type, stob_path,
					&stob->rs_ldom);
	if (rc == 0) {
	     sdom = stob->rs_ldom;
	     rc = c2_linux_stob_setup(sdom, false);
	}

	return rc;
}

void cs_ad_stob_fini(struct c2_cs_reqh_stobs *stob)
{
	C2_PRE(stob != NULL);

        if (stob->rs_adom != NULL) {
             if (stob->rs_stob_back != NULL &&
                 stob->rs_stob_back->so_state == CSS_EXISTS) {
                 c2_dtx_done(&stob->rs_tx);
                 c2_stob_put(stob->rs_stob_back);
             }
             stob->rs_adom->sd_ops->sdo_fini(stob->rs_adom);
        }
}

void cs_linux_stob_fini(struct c2_cs_reqh_stobs *stob)
{
	C2_PRE(stob != NULL);

	if (stob->rs_ldom != NULL)
                stob->rs_ldom->sd_ops->sdo_fini(stob->rs_ldom);
}


int c2_cs_storage_init(const char *stob_type, const char *stob_path,
		       struct c2_cs_reqh_stobs *stob, struct c2_dbenv *db)
{
	int               rc;
	int               slen;
	char             *objpath;
	static const char objdir[] = "/o";

	C2_PRE(stob_type != NULL && stob_path != NULL && stob != NULL);

	stob->rs_stype = stob_type;

	slen = strlen(stob_path);
	C2_ALLOC_ARR(objpath, slen + ARRAY_SIZE(objdir));
	if (objpath == NULL)
		return -ENOMEM;

	sprintf(objpath, "%s%s", stob_path, objdir);

	rc = mkdir(stob_path, 0700);
        if (rc != 0 && errno != EEXIST)
		goto out;

        rc = mkdir(objpath, 0700);
        if (rc != 0 && errno != EEXIST)
		goto out;

	rc = cs_linux_stob_init(stob_path, stob);
	if (rc != 0)
		goto out;

	if (strcasecmp(stob_type, cs_stobs[AD_STOB]) == 0)
		rc = cs_ad_stob_init(stob_path, stob, db);

out:
	c2_free(objpath);

	return rc;
}

void c2_cs_storage_fini(struct c2_cs_reqh_stobs *stob)
{
	C2_PRE(stob != NULL);

        cs_ad_stob_fini(stob);
        cs_linux_stob_fini(stob);
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

	rc = c2_reqh_service_locate(stype, &service);
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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		C2_ASSERT(cs_reqh_context_invariant(rctx));

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			rc = cs_service_init(rctx->rc_services[idx],
						&rctx->rc_reqh);
			if (rc != 0)
				return rc;
		}
	} c2_tlist_endfor;

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
        c2_tlist_for(&c2_rhsvc_tl, services, service) {
		C2_ASSERT(c2_reqh_service_invariant(service));
		cs_service_fini(service);
	} c2_tlist_endfor;
}

/**
   Initialises network domains per given distinct xport:endpoint pair in a
   colibri context.

   @param cctx Colibri context
 */
static int cs_net_domains_init(struct c2_colibri *cctx)
{
	int                          rc;
	int                          xprts_nr;
	FILE                        *ofd;
	struct c2_net_xprt         **xprts;
	struct c2_net_xprt          *xprt;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	xprts = cctx->cc_xprts;
	xprts_nr = cctx->cc_xprts_nr;

	ofd = cctx->cc_outfile;
        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		C2_ASSERT(cs_reqh_context_invariant(rctx));

		c2_tlist_for(&cs_eps_tl, &rctx->rc_eps, ep) {
			C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));

			struct c2_net_domain *ndom;
			xprt = cs_xprt_lookup(ep->ex_xprt, xprts, xprts_nr);
			if (xprt == NULL) {
				fprintf(ofd,
					"COLIBRI: Invalid Transport: %s\n",
					ep->ex_xprt);
				return -EINVAL;
			}

			ndom = cs_net_domain_locate(cctx, ep->ex_xprt);
			if (ndom != NULL)
				continue;

			rc = c2_net_xprt_init(xprt);
			if (rc != 0)
				return rc;

			C2_ALLOC_PTR(ndom);
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
			ndom_tlink_init(ndom);
			c2_net_domain_bob_init(ndom);
			ndom_tlist_add_tail(&cctx->cc_ndoms, ndom);
		} c2_tlist_endfor;
	} c2_tlist_endfor;

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
	int                    idx;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	xprts = cctx->cc_xprts;
	c2_tlist_for(&ndom_tl, &cctx->cc_ndoms, ndom) {
		C2_ASSERT(c2_net_domain_bob_check(ndom));
		c2_net_domain_fini(ndom);
		ndom_tlist_del(ndom);
		c2_net_domain_bob_fini(ndom);
		ndom_tlink_fini(ndom);
		c2_free(ndom);
	} c2_tlist_endfor;

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
	struct c2_cs_reqh_stobs *rstob;
	struct c2_stob_domain   *sdom;

	rc = c2_dbenv_init(&rctx->rc_db, rctx->rc_dbpath, 0);
	if (rc != 0)
		goto out;

	rc = c2_cs_storage_init(rctx->rc_stype, rctx->rc_stpath,
                                     &rctx->rc_stob, &rctx->rc_db);
	if (rc != 0)
		goto cleanup_db;

	rctx->rc_cdom_id.id = ++cdom_id;
	rc = c2_cob_domain_init(&rctx->rc_cdom, &rctx->rc_db,
					&rctx->rc_cdom_id);
	if (rc != 0)
		goto cleanup_stob;

	rc = c2_fol_init(&rctx->rc_fol, &rctx->rc_db);
	if (rc != 0)
		goto cleanup_cob;

	rstob = &rctx->rc_stob;
	if (strcasecmp(rstob->rs_stype, cs_stobs[AD_STOB]) == 0)
		sdom = rstob->rs_adom;
	else
		sdom = rstob->rs_ldom;

	rc = c2_reqh_init(&rctx->rc_reqh, NULL, sdom, &rctx->rc_db,
					&rctx->rc_cdom, &rctx->rc_fol);
	if (rc != 0)
		goto cleanup_fol;

	rctx->rc_state = RC_INITIALISED;
	goto out;

cleanup_fol:
	c2_fol_fini(&rctx->rc_fol);
cleanup_cob:
	c2_cob_domain_fini(&rctx->rc_cdom);
cleanup_stob:
	c2_cs_storage_fini(&rctx->rc_stob);
cleanup_db:
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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));
        if(rhctx_tlist_is_empty(&cctx->cc_reqh_ctxs))
		return -EINVAL;

	ofd = cctx->cc_outfile;
        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		C2_ASSERT(cs_reqh_context_invariant(rctx));

		rc = cs_request_handler_start(rctx);
		if (rc != 0) {
			fprintf(ofd,
				"COLIBRI: Failed to start request handler,"
				 "rc=%d\n", rc);
			return rc;
		}
	} c2_tlist_endfor;

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
	c2_time_t       rdelay;
	uint64_t        sleepcnt;

	C2_PRE(cs_reqh_context_invariant(rctx));
	C2_PRE(cs_reqh_context_bob_check(rctx));

	reqh = &rctx->rc_reqh;
        c2_mutex_lock(&reqh->rh_lock);
        reqh->rh_shutdown = true;
        c2_mutex_unlock(&reqh->rh_lock);

	sleepcnt = 1;
	while (!c2_reqh_can_shutdown(reqh)) {
		c2_nanosleep(c2_time_set(&rdelay, 0,
			WAIT_FOR_REQH_SHUTDOWN * sleepcnt), NULL);
                ++sleepcnt;
	}

	cs_services_fini(reqh);
	cs_rpc_machines_fini(reqh);
	c2_reqh_fini(reqh);
	c2_fol_fini(&rctx->rc_fol);
	c2_cob_domain_fini(&rctx->rc_cdom);
	c2_cs_storage_fini(&rctx->rc_stob);
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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		if (rctx->rc_state == RC_INITIALISED)
			cs_request_handler_stop(rctx);
		cs_reqh_ctx_free(rctx);
	} c2_tlist_endfor;
}

enum COBFID_MAP_DB_OP {
	COBFID_MAP_DB_RECADD,
	COBFID_MAP_DB_RECDEL,
};

static const struct c2_addb_loc cobfid_map_setup_loc = {
	.al_name = "cobfid_map_setup_loc",
};

static const struct c2_addb_ctx_type cobfid_map_setup_addb = {
	.act_name = "cobfid_map_setup",
};

C2_ADDB_EV_DEFINE(cobfid_map_setup_func_fail, "cobfid_map_setup_func_failed",
		  C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
   Find a request handler service within a given Colibir instance.

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
	struct c2_reqh		*reqh = NULL;

	C2_PRE(cctx != NULL);
	C2_PRE(service_name != NULL);

	c2_mutex_lock(&cctx->cc_mutex);
        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));
		C2_ASSERT(cs_reqh_context_invariant(rctx));

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			if (strcmp(rctx->rc_services[idx],
					service_name) == 0) {
				reqh = &rctx->rc_reqh;
				break;
			}
		}
	} c2_tlist_endfor;
	c2_mutex_unlock(&cctx->cc_mutex);

	return reqh;

}
C2_EXPORTED(c2_cs_reqh_get);

struct c2_colibri *c2_cs_ctx_get(struct c2_reqh_service *s)
{
	struct cs_reqh_context *rqctx;
	struct c2_colibri      *cc;

	C2_PRE(s != NULL);

	c2_mutex_lock(&s->rs_mutex);
	rqctx = container_of(s->rs_reqh, struct cs_reqh_context, rc_reqh);
	cc = rqctx->rc_colibri;
	c2_mutex_unlock(&s->rs_mutex);
	return cc;
}

/**
 * c2_cobfid_setup is initialized by the very first ioservice that
 * runs on a Colirbi data server. It is refcounted.
 */
int c2_cobfid_setup_get(struct c2_cobfid_setup **out, struct c2_colibri *cc)
{
	int rc = 0;

	C2_PRE(out != NULL);
	C2_PRE(cc != NULL);
	C2_PRE(c2_mutex_is_locked(&cc->cc_mutex));

	if (cc->cc_setup == NULL) {
		C2_ALLOC_PTR(cc->cc_setup);
		if (cc->cc_setup == NULL)
			return -ENOMEM;

		rc = cobfid_map_setup_init(cc, cobfid_map_name);
		if (rc != 0) {
			c2_free(cc->cc_setup);
			return rc;
		}
	} else
		c2_ref_get(&cc->cc_setup->cms_refcount);

	*out = cc->cc_setup;
	return rc;
}

/**
 * Releases reference on c2_cobfid_setup structure. Last reference put
 * will finalize the structure.
 */
void c2_cobfid_setup_put(struct c2_colibri *cc)
{
	C2_PRE(cc != NULL);
	C2_PRE(c2_mutex_is_locked(&cc->cc_mutex));

	c2_ref_put(&cc->cc_setup->cms_refcount);
}

static int cobfid_map_setup_init(struct c2_colibri *cc, const char *name)
{
	int rc;
	struct c2_cobfid_setup *s;

	C2_PRE(cc != NULL);
	C2_PRE(cc->cc_setup != NULL);
	C2_PRE(name != NULL);

	s = cc->cc_setup;

	rc = c2_dbenv_init(&s->cms_dbenv, name, 0);
	if (rc != 0) {
		C2_ADDB_ADD(&s->cms_addb, &cobfid_map_setup_loc,
			    cobfid_map_setup_func_fail,
			    "c2_dbenv_init() failed.", rc);
		return rc;
	}

	rc = c2_cobfid_map_init(&s->cms_map, &s->cms_dbenv, &s->cms_addb, name);
	if (rc != 0) {
		C2_ADDB_ADD(&s->cms_addb, &cobfid_map_setup_loc,
			    cobfid_map_setup_func_fail,
			    "c2_cobfid_map_init() failed.", rc);
		c2_dbenv_fini(&s->cms_dbenv);
		return rc;
	}

	c2_ref_init(&s->cms_refcount, 1, cobfid_map_setup_fini);
	c2_mutex_init(&s->cms_mutex);
	c2_addb_ctx_init(&s->cms_addb, &cobfid_map_setup_addb,
			 &c2_addb_global_ctx);
	s->cms_colibri = cc;
	return rc;
}

static void cobfid_map_setup_fini(struct c2_ref *ref)
{
	struct c2_colibri      *cc;
	struct c2_cobfid_setup *s;

	C2_PRE(ref != NULL);

	s = container_of(ref, struct c2_cobfid_setup, cms_refcount);
	cc = s->cms_colibri;
	c2_cobfid_map_fini(&s->cms_map);
	c2_dbenv_fini(&s->cms_dbenv);
	c2_addb_ctx_fini(&s->cms_addb);
	c2_mutex_fini(&s->cms_mutex);
	c2_free(s);
	cc->cc_setup = NULL;
}

static int cobfid_map_setup_process(struct c2_cobfid_setup *s,
				    struct c2_fid gfid, struct c2_uint128 cfid,
				    enum COBFID_MAP_DB_OP op)
{
	int rc;

	C2_PRE(s != NULL);
	C2_PRE(op == COBFID_MAP_DB_RECADD || op == COBFID_MAP_DB_RECDEL);

	c2_mutex_lock(&s->cms_mutex);

	if (op == COBFID_MAP_DB_RECADD)
		rc = c2_cobfid_map_add(&s->cms_map, cfid.u_hi, gfid, cfid);
	else
		rc = c2_cobfid_map_del(&s->cms_map, cfid.u_hi, gfid);

	c2_mutex_unlock(&s->cms_mutex);

	if (rc != 0)
		C2_ADDB_ADD(&s->cms_addb, &cobfid_map_setup_loc,
			    cobfid_map_setup_func_fail,
			    "c2_cobfid_map_adddel() failed.", rc);
	return rc;
}

int c2_cobfid_setup_recadd(struct c2_cobfid_setup *s,
			   struct c2_fid gfid,
			   struct c2_uint128 cfid)
{
	return cobfid_map_setup_process(s, gfid, cfid, COBFID_MAP_DB_RECADD);
}

int c2_cobfid_setup_recdel(struct c2_cobfid_setup *s,
			   struct c2_fid gfid,
			   struct c2_uint128 cfid)
{
	return cobfid_map_setup_process(s, gfid, cfid, COBFID_MAP_DB_RECDEL);
}

/**
   Initialises a colibri context.

   @param cctx Colibri context to be initialised

   @pre cctx != NULL
 */
static int cs_colibri_init(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	rhctx_tlist_init(&cctx->cc_reqh_ctxs);
	c2_bob_type_tlist_init(&rhctx_bob, &rhctx_tl);

	ndom_tlist_init(&cctx->cc_ndoms);
	c2_bob_type_tlist_init(&ndom_bob, &ndom_tl);
        cs_buffer_pools_tlist_init(&cctx->cc_buffer_pools);

	return 0;
}

/**
   Finalises a colibri context.

   @pre cctx != NULL

   @param cctx Colibri context to be finalised
 */
static void cs_colibri_fini(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

	cs_buffer_pool_fini(cctx);
	cs_net_domains_fini(cctx);
	rhctx_tlist_fini(&cctx->cc_reqh_ctxs);
	ndom_tlist_fini(&cctx->cc_ndoms);
        cs_buffer_pools_tlist_fini(&cctx->cc_buffer_pools);
}

/**
   Displays usage of colibri_setup program.

   @param f File to which the output is written
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
		   " -SStobFile {-e xport:endpoint}+\n"
		   "                        {-s service}+"
		   " [-q MinReceiveQueueLength] [-m RPCMaxMessageSize]\n");
}

/**
   Displays help for colibri_setup program.

   @param f File to which the output is written
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
		   "-e Network layer endpoint to which clients connect. "
		   "Network layer endpoint\n   consists of 2 parts "
		   "network transport:endpoint address.\n"
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
		   "-s Services to be started in given request handler "
		   "context.\n   This can be specified multiple times "
		   "per request handler set.\n"
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

static uint32_t cs_domain_tms_nr(struct c2_colibri *cctx,
				struct c2_net_domain *dom)
{
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;
        uint32_t		     cnt = 0;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));
        C2_ASSERT(!rhctx_tlist_is_empty(&cctx->cc_reqh_ctxs));

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
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

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

static int reqh_ctxs_are_valid(struct c2_colibri *cctx)
{
	int                          rc = 0;
	int                          idx;
	FILE                        *ofd;
	struct cs_reqh_context      *rctx;
	struct cs_endpoint_and_xprt *ep;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));
        C2_ASSERT(!rhctx_tlist_is_empty(&cctx->cc_reqh_ctxs));

	if (cctx->cc_recv_queue_min_length < C2_NET_TM_RECV_QUEUE_DEF_LEN)
		cctx->cc_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN;

	ofd = cctx->cc_outfile;
        c2_tlist_for(&rhctx_tl, &cctx->cc_reqh_ctxs, rctx) {
		C2_ASSERT(cs_reqh_context_bob_check(rctx));

                if (!cs_reqh_context_invariant(rctx)) {
                        fprintf(ofd,
				"COLIBRI: Missing or invalid parameters\n");
			cs_usage(ofd);
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
                        fprintf(ofd, "COLIBRI: Invalid storage type\n");
			cs_stob_types_list(ofd);
                        return -EINVAL;
                }
		/*
		   Check if all the given end points in a reqh context are
		   valid.
		 */
		if (cs_eps_tlist_is_empty(&rctx->rc_eps)) {
			fprintf(ofd, "COLIBRI: No endpoint specified\n");
			return -EINVAL;
		}
		c2_tlist_for(&cs_eps_tl, &rctx->rc_eps, ep) {
			C2_ASSERT(cs_endpoint_and_xprt_bob_check(ep));
			rc = cs_endpoint_validate(cctx, ep->ex_endpoint,
						  ep->ex_xprt);
			if (rc == -EADDRINUSE)
				fprintf(ofd,
					"COLIBRI: Duplicate end point: %s:%s\n",
					ep->ex_xprt, ep->ex_endpoint);
			else if (rc == -EINVAL)
				fprintf(ofd,
					"COLIBRI: Invalid endpoint: %s:%s\n",
					ep->ex_xprt, ep->ex_endpoint);
			if (rc != 0)
				return rc;
		} c2_tlist_endfor;

		/*
		   Check if the services are registered and are valid in a
		   reqh context.
		 */
		if (rctx->rc_snr == 0) {
			fprintf(ofd, "COLIBRI: Services unavailable\n");
			return -EINVAL;
		}

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			if (!service_is_registered(rctx->rc_services[idx])) {
				fprintf(ofd, "COLIBRI: Unknown service: %s\n",
                                                       rctx->rc_services[idx]);
				cs_services_list(ofd);
				return -ENOENT;
			}
			if (service_is_duplicate(cctx,
						 rctx->rc_services[idx])) {
				fprintf(ofd, "COLIBRI: Duplicate service: %s\n",
                                                       rctx->rc_services[idx]);
				return -EADDRINUSE;
			}
		}
	} c2_tlist_endfor;

	return rc;
}

/**
   Parses given arguments and allocates request handler context, if all the
   required arguments are provided and valid.
   Every allocated request handler context is added to the list of the same
   in given colibri context.

   @param cctx Colibri context to be setup
 */
static int cs_parse_args(struct c2_colibri *cctx, int argc, char **argv)
{
	int                     rc = 0;
	int                     result;
	struct cs_reqh_context *rctx = NULL;
	FILE                   *ofd;

	C2_PRE(cctx != NULL);
	C2_PRE(c2_mutex_is_locked(&cctx->cc_mutex));

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
					cs_services_list(ofd);
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
			C2_STRINGARG('S', "Storage name",
				LAMBDA(void, (const char *str)
				{
					if (rctx == NULL) {
						rc = -EINVAL;
						return;
					}
				rctx->rc_stpath = str;
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
						fprintf(ofd,
							"Too many services\n");
						rc = -E2BIG;
						return;
					}
					rctx->rc_services[rctx->rc_snr] = str;
					C2_CNT_INC(rctx->rc_snr);
				})));
	return result != 0 ? result : rc;
}

int c2_cs_setup_env(struct c2_colibri *cctx, int argc, char **argv)
{
	int rc;

	C2_PRE(cctx != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	c2_mutex_lock(&cctx->cc_mutex);
	rc = cs_parse_args(cctx, argc, argv);
	if (rc < 0) {
		cs_usage(cctx->cc_outfile);
		c2_mutex_unlock(&cctx->cc_mutex);
		return rc;
	}

	if (rc == 0)
		rc = reqh_ctxs_are_valid(cctx);

	if (rc == 0)
		rc = cs_net_domains_init(cctx);

	if (rc == 0)
		rc = cs_buffer_pool_setup(cctx);

	if (rc == 0)
		rc = cs_request_handlers_start(cctx);

	if (rc == 0)
		rc = cs_rpc_machines_init(cctx);

	c2_mutex_unlock(&cctx->cc_mutex);
	return rc;
}

int c2_cs_start(struct c2_colibri *cctx)
{
	int rc;

	C2_PRE(cctx != NULL);

	c2_mutex_lock(&cctx->cc_mutex);
	rc = cs_services_init(cctx);
	if (rc != 0)
		fprintf(cctx->cc_outfile,
			"COLIBRI: Service initialisation failed\n");

	c2_mutex_unlock(&cctx->cc_mutex);
	return rc;
}

int c2_cs_init(struct c2_colibri *cctx, struct c2_net_xprt **xprts,
	       int xprts_nr, FILE *out)
{
        int rc;

        C2_PRE(cctx != NULL && xprts != NULL && xprts_nr > 0 && out != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	c2_mutex_init(&cctx->cc_mutex);

	c2_mutex_lock(&cctx->cc_mutex);
        cctx->cc_xprts = xprts;
	cctx->cc_xprts_nr = xprts_nr;
	cctx->cc_outfile = out;

	rc = cs_colibri_init(cctx);
	C2_ASSERT(rc == 0);

        rc = c2_processors_init();
	c2_mutex_unlock(&cctx->cc_mutex);
	return rc;
}

void c2_cs_fini(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);

	c2_mutex_lock(&cctx->cc_mutex);
        cs_request_handlers_stop(cctx);
        cs_colibri_fini(cctx);
	c2_mutex_unlock(&cctx->cc_mutex);

	c2_mutex_fini(&cctx->cc_mutex);
	c2_processors_fini();
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
