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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
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

#include "stob/ad.h"
#include "stob/linux.h"
#include "net/net.h"
#include "rpc/rpc2.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "balloc/balloc.h"
#include "colibri/colibri_setup.h"

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
	/** Hex value for "csrhctxs" */
	CS_REQH_CTX_MAGIC = 0x6373726863747873,
	/** Hex value for "csnetdom" */
	CS_NET_DOMS_MAGIC = 0x63736e6574646f6d
};

enum {
	WAIT_FOR_REQH_SHUTDOWN = 1000000,
};

extern const struct c2_tl_descr c2_rstypes_descr;
extern struct c2_tl		c2_rstypes;
extern struct c2_mutex		c2_rstypes_mutex;

extern const struct c2_tl_descr c2_rh_sl_descr;
extern const struct c2_tl_descr c2_rh_rpml_descr;

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
	   3-tuple network layer endpoint address.
	   e.g. 127.0.0.1:1024:1
	 */
	const char *endpoint;
	/** Supported network transport. */
	const char *xprt;
	/**
	   Scratch buffer for endpoint and transport extraction.
	 */
	char *scrbuf;
};

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
	struct cs_endpoint_and_xprt *rc_eps;

	/** Number of endpoints configured per request handler context. */
	int                          rc_enr;

	/**
	    Maximum number of endpoints allowed per request handler context.
	 */
	int                          rc_max_eps;

	/**
	    State of a request handler context, i.e. RC_INITIALISED or
	    RC_UNINTIALISED.
	 */
	int                          rc_state;

	/** Storage domain for a request handler */
	struct c2_cs_reqh_stobs         rc_stob;

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

extern struct c2_balloc colibri_balloc;

static const struct c2_tl_descr rctx_descr = C2_TL_DESCR("reqh contexts",
                                                         struct cs_reqh_context,
                                                         rc_linkage,
                                                         rc_magic,
                                                         CS_REQH_CTX_MAGIC,
                                                         CS_REQH_CTX_MAGIC);

static const struct c2_tl_descr ndoms_descr = C2_TL_DESCR("network domains",
							  struct c2_net_domain,
							  nd_app_linkage,
							  nd_magic,
							  C2_NET_MAGIC,
							  CS_NET_DOMS_MAGIC);

static struct c2_net_domain *cs_net_domain_locate(struct c2_colibri *cctx,
						  const char *xprt);
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
        int   i;

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

	if (c2_tlist_is_empty(&c2_rstypes_descr, &c2_rstypes)) {
		fprintf(out, "No available services\n");
		return;
	}

	fprintf(out, "Supported services:\n");
	c2_tlist_for(&c2_rstypes_descr, &c2_rstypes, stype) {
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
	int                      cnt;
	int                      idx;
	struct cs_reqh_context  *rctx;

	C2_PRE(cctx != NULL && xprt != NULL && ep != NULL);

        C2_ASSERT(!c2_tlist_is_empty(&rctx_descr, &cctx->cc_reqh_ctxs));

	c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {
		for (idx = 0, cnt = 0; idx < rctx->rc_enr; ++idx) {
			if (strcmp(rctx->rc_eps[idx].endpoint, ep) == 0 ||
                            strcmp(rctx->rc_eps[idx].xprt, xprt->nx_name) == 0)
				++cnt;
			if (cnt > 1)
				return true;
		}
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
static int ep_and_xprt_get(struct cs_endpoint_and_xprt *ep_xprt, const char *ep)
{
	char *sptr;

	C2_PRE(ep_xprt != NULL && ep != NULL);

	C2_ALLOC_ARR(ep_xprt->scrbuf, strlen(ep) + 1);
	strcpy(ep_xprt->scrbuf, ep);
	ep_xprt->xprt = strtok_r(ep_xprt->scrbuf, ":", &sptr);
	if (ep_xprt->xprt == NULL)
		return -EINVAL;

	ep_xprt->endpoint = strtok_r(NULL , "\0", &sptr);
	if (ep_xprt->endpoint == NULL)
		return -EINVAL;

	return 0;
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

        cnt = 0;
        c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {
                for (idx = 0; idx < rctx->rc_snr; ++idx) {
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

	c2_tlist_for(&c2_rstypes_descr, &c2_rstypes, stype) {
		if (strcasecmp(stype->rst_name, service_name) == 0)
			return true;
	} c2_tlist_endfor;

	return false;
}

struct c2_rpcmachine *c2_cs_rpcmach_get(struct c2_colibri *cctx,
					const struct c2_net_xprt *xprt,
					const char *sname)
{
	struct c2_reqh            *reqh;
	struct cs_reqh_context    *rctx;
	struct c2_reqh_service    *service;
	struct c2_rpcmachine      *rpcmach;
	struct c2_net_xprt        *nxprt;

        C2_PRE(cctx != NULL);

        C2_ASSERT(!c2_tlist_is_empty(&rctx_descr, &cctx->cc_reqh_ctxs));

        c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {
		reqh = &rctx->rc_reqh;
                c2_tlist_for(&c2_rh_sl_descr, &reqh->rh_services, service) {
			if (strcmp(service->rs_type->rst_name, sname) != 0)
				continue;
			c2_tlist_for(&c2_rh_rpml_descr, &reqh->rh_rpcmachines,
								rpcmach) {
				nxprt = rpcmach->cr_tm.ntm_dom->nd_xprt;
				C2_ASSERT(nxprt != NULL);
				if (strcmp(nxprt->nx_name, xprt->nx_name) == 0)
					return rpcmach;
			} c2_tlist_endfor;
                } c2_tlist_endfor;
        } c2_tlist_endfor;

        return NULL;

}
C2_EXPORTED(c2_cs_rpcmach_get);

struct c2_net_transfer_mc *c2_cs_tm_get(struct c2_colibri *cctx,
					const struct c2_net_xprt *xprt,
					const char *sname)
{
	struct c2_rpcmachine *rpcmach;

	rpcmach = c2_cs_rpcmach_get(cctx, xprt, sname);

	return (rpcmach == NULL) ? NULL : &rpcmach->cr_tm;
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
			rctx->rc_stpath != NULL && rctx->rc_enr != 0 &&
			rctx->rc_snr != 0;
	case RC_INITIALISED:
		return rctx->rc_magic == CS_REQH_CTX_MAGIC &&
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

	C2_ALLOC_PTR(rctx);
	if (rctx == NULL)
		goto out;

	rctx->rc_max_eps = cctx->cc_xprts_nr;
	C2_ALLOC_ARR(rctx->rc_eps, rctx->rc_max_eps);
	if (rctx->rc_eps == NULL)
		goto cleanup_rctx;

	rctx->rc_max_services = c2_tlist_length(&c2_rstypes_descr, &c2_rstypes);
	if (rctx->rc_max_services == 0) {
		fprintf(cctx->cc_outfile, "No available services\n");
		goto cleanup_endpoints;
	}

	C2_ALLOC_ARR(rctx->rc_services, rctx->rc_max_services);
	if (rctx->rc_services == NULL)
		goto cleanup_endpoints;

	rctx->rc_magic = CS_REQH_CTX_MAGIC;
	c2_tlink_init(&rctx_descr, rctx);
	c2_tlist_add_tail(&rctx_descr, &cctx->cc_reqh_ctxs, rctx);

	goto out;

cleanup_endpoints:
	c2_free(rctx->rc_eps);
cleanup_rctx:
	c2_free(rctx);
	rctx = NULL;
out:
	return rctx;
}

static void cs_reqh_ctx_free(struct cs_reqh_context *rctx)
{
	int i;

	C2_PRE(rctx != NULL);

	for (i = 0; i < rctx->rc_max_eps; ++i) {
		if (rctx->rc_eps[i].scrbuf != NULL)
			c2_free(rctx->rc_eps[i].scrbuf);
	}
	c2_free(rctx->rc_eps);
	c2_free(rctx->rc_services);
	c2_tlist_del(&rctx_descr, rctx);
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

        C2_ASSERT(!c2_tlist_is_empty(&ndoms_descr, &cctx->cc_ndoms));

	c2_tlist_for(&ndoms_descr, &cctx->cc_ndoms, ndom) {
		if (strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0)
			return ndom;
	} c2_tlist_endfor;

	return NULL;
}

/**
   Initialises rpc machine for the given endpoint address.
   Once the new rpcmachine is created it is added to list of rpc machines
   in given request handler.
   Request handler should be initialised before invoking this function.

   @param cctx Colibri context
   @param xprt_name Network transport
   @param ep Network endpoint address
   @param reqh Request handler to which the newly created
		rpcmachine belongs

   @pre cctx != NULL && xprt_name != NULL && ep != NULL && reqh != NULL
 */
static int cs_rpcmachine_init(struct c2_colibri *cctx, const char *xprt_name,
			      const char *ep, struct c2_reqh *reqh)
{
	struct c2_rpcmachine         *rpcmach;
	struct c2_net_domain         *ndom;
	struct c2_net_xprt           *xprt;
	int                           rc;

	C2_PRE(cctx != NULL && xprt_name != NULL && ep != NULL &&
	       reqh != NULL);

	xprt = cs_xprt_lookup(xprt_name, cctx->cc_xprts, cctx->cc_xprts_nr);

	if (xprt == NULL)
		return -EINVAL;

	ndom = cs_net_domain_locate(cctx, xprt->nx_name);
	if (ndom == NULL)
		return -EINVAL;

	C2_ALLOC_PTR(rpcmach);
	if (rpcmach == NULL)
		return -ENOMEM;

	rc = c2_rpcmachine_init(rpcmach, reqh->rh_cob_domain, ndom, ep, reqh);
	if (rc != 0) {
		c2_free(rpcmach);
		return rc;
	}

	c2_tlink_init(&c2_rh_rpml_descr, rpcmach);
	c2_tlist_add_tail(&c2_rh_rpml_descr, &reqh->rh_rpcmachines, rpcmach);

	return rc;
}

/**
   Intialises rpc machines in a colibri context.

   @param cctx Colibri context
 */
static int cs_rpcmachines_init(struct c2_colibri *cctx)
{
	int                      idx;
	int                      rc;
	FILE                    *ofd;
	struct  cs_reqh_context *rctx;

	C2_PRE(cctx != NULL);

        C2_ASSERT(!c2_tlist_is_empty(&rctx_descr, &cctx->cc_reqh_ctxs));

	ofd = cctx->cc_outfile;
        c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {

		C2_ASSERT(cs_reqh_context_invariant(rctx));

		for (idx = 0; idx < rctx->rc_enr; ++idx) {
			rc = cs_rpcmachine_init(cctx,
						rctx->rc_eps[idx].xprt,
						rctx->rc_eps[idx].endpoint,
						&rctx->rc_reqh);
			if (rc != 0) {
				fprintf(ofd,
					"COLIBRI: Invalid endpoint: %s:%s\n",
					rctx->rc_eps[idx].xprt,
					rctx->rc_eps[idx].endpoint);
				return rc;
			}
		}
	} c2_tlist_endfor;

	return rc;
}

/**
   Finalises all the rpc machines from the list of rpc machines present in
   c2_reqh.

   @param reqh Request handler of which the rpc machines belong

   @pre reqh != NULL
 */
static void cs_rpcmachines_fini(struct c2_reqh *reqh)
{
	struct c2_rpcmachine *rpcmach;

	C2_PRE(reqh != NULL);

	c2_tlist_for(&c2_rh_rpml_descr, &reqh->rh_rpcmachines, rpcmach) {
		C2_ASSERT(rpcmach != NULL);
		c2_rpcmachine_fini(rpcmach);
		c2_tlist_del(&c2_rh_rpml_descr, rpcmach);
		c2_tlink_fini(&c2_rh_rpml_descr, rpcmach);
		c2_free(rpcmach);
	} c2_tlist_endfor;
}

/**
   Initialises AD type stob.
 */
static int cs_ad_stob_init(const char *stob_path, struct c2_cs_reqh_stobs *stob,
			   struct c2_dbenv *db, struct c2_stob **bstob)
{
	int rc;

	rc = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type,
				stob_path, &stob->adstob);

	if (rc == 0)
		rc = c2_ad_stob_setup(stob->adstob, db, *bstob,
				      &colibri_balloc.cb_ballroom,
				      BALLOC_DEF_CONTAINER_SIZE,
				      BALLOC_DEF_BLOCK_SHIFT,
				      BALLOC_DEF_BLOCKS_PER_GROUP,
				      BALLOC_DEF_RESERVED_GROUPS);
	return rc;
}

/**
   Initialises linux type stob.
 */
static int cs_linux_stob_init(const char *stob_path,
			      struct c2_cs_reqh_stobs *stob,
			      struct c2_stob **bstob)
{
	int                    rc;
	struct c2_stob_domain *sdom;

	rc = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type,
					stob_path, &stob->linuxstob);
	if  (rc == 0) {
		sdom = stob->linuxstob;
		rc = c2_linux_stob_setup(sdom, false);
		if  (rc == 0)
			rc = c2_stob_find(sdom, &stob->stob_id, bstob);
	}

	return rc;
}

int c2_cs_storage_init(const char *stob_type, const char *stob_path,
		       struct c2_cs_reqh_stobs *stob, struct c2_dbenv *db)
{
	int                      rc;
	int                      slen;
	char                    *objpath;
        struct c2_stob          *bstore;
	static const char        objdir[] = "/o";

	C2_PRE(stob_type != NULL && stob_path != NULL && stob != NULL);

	stob->stype = stob_type;

	/*
	   XXX Need generic mechanism to generate stob ids
	 */
        stob->stob_id.si_bits = (struct c2_uint128){ .u_hi = 0x0,
						     .u_lo = 0xdf11e };
	slen = strlen(stob_path);
	C2_ALLOC_ARR(objpath, slen + ARRAY_SIZE(objdir));
	if (objpath == NULL)
		return -ENOMEM;

	sprintf(objpath, "%s%s", stob_path, objdir);

	rc = mkdir(stob_path, 0700);
        if (rc != 0 && errno != EEXIST)
		goto cleanup;

        rc = mkdir(objpath, 0700);
        if (rc != 0 && errno != EEXIST)
		goto cleanup;

	rc = cs_linux_stob_init(stob_path, stob, &bstore);
	if (rc != 0)
		goto cleanup;

	rc = c2_stob_create(bstore, NULL);
	if (rc != 0)
		goto cleanup;

	if (strcasecmp(stob_type, cs_stobs[AD_STOB]) == 0)
		rc = cs_ad_stob_init(stob_path, stob, db, &bstore);

	if (rc != 0)
		goto cleanup;

	C2_ASSERT(bstore->so_state == CSS_EXISTS);

cleanup:
	if (bstore != NULL)
		c2_stob_put(bstore);
	c2_free(objpath);

	return rc;
}

void c2_cs_storage_fini(struct c2_cs_reqh_stobs *stob)
{
	C2_PRE(stob != NULL);

	if (stob->linuxstob != NULL) {
		if (stob->adstob != NULL)
			stob->adstob->sd_ops->sdo_fini(stob->adstob);
		stob->linuxstob->sd_ops->sdo_fini(stob->linuxstob);
	}
}

/**
   Initialises and starts a particular service.
   Once the service is initialised it is started and registered with the
   appropriate request handler.

   @param service_name Name of service to be initialised
   @param reqh Request handler this service is registered with

   @pre service_name != NULL && reqh != NULL

   @post c2_reqh_service_invariant(service)
 */
static int cs_service_init(const char *service_name, struct c2_reqh *reqh)
{
	int                           rc;
	struct c2_reqh_service_type  *stype;
	struct c2_reqh_service       *service;

	C2_PRE(service_name != NULL && reqh != NULL);

        stype = c2_reqh_service_type_find(service_name);
        if (stype == NULL) {
                rc = -EINVAL;
                goto out;
        }

	rc = stype->rst_ops->rsto_service_alloc_and_init(stype, &service);
	if (rc != 0)
		goto out;

	C2_ASSERT(c2_reqh_service_invariant(service));

	rc = c2_reqh_service_init(service, reqh);
	if (rc != 0) {
		service->rs_phase = C2_RSPH_FAILED;
		goto cleanup3;
	}

	C2_ASSERT(c2_reqh_service_invariant(service));

	service->rs_phase = C2_RSPH_STARTING;
	rc = service->rs_ops->rso_start(service);
	if (rc != 0) {
		service->rs_phase = C2_RSPH_FAILED;
		goto cleanup2;
	}

	rc = c2_reqh_service_start(service);
	if (rc != 0) {
		service->rs_phase = C2_RSPH_FAILED;
		goto cleanup1;
	}

	C2_POST(c2_reqh_service_invariant(service));
	goto out;

cleanup1:
	service->rs_ops->rso_stop(service);
cleanup2:
	c2_reqh_service_fini(service);
cleanup3:
	service->rs_ops->rso_fini(service);
out:
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
	int                      idx;
	int                      rc;
	struct cs_reqh_context  *rctx;

	C2_PRE(cctx != NULL);

        c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {
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

	service->rs_phase = C2_RSPH_STOPPING;
	service->rs_ops->rso_stop(service);
	c2_reqh_service_stop(service);
	C2_ASSERT(service->rs_phase == C2_RSPH_STOPPED &&
			service->rs_state == C2_RSS_STOPPED);
	c2_reqh_service_fini(service);
	service->rs_ops->rso_fini(service);
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
        c2_tlist_for(&c2_rh_sl_descr, services, service) {
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
	int                     idx;
	int                     rc;
	int                     xprts_nr;
	struct c2_net_xprt    **xprts;
	struct c2_net_xprt     *xprt;
	struct cs_reqh_context *rctx;

	C2_PRE(cctx != NULL);

	xprts = cctx->cc_xprts;
	xprts_nr = cctx->cc_xprts_nr;

        c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {

		C2_ASSERT(cs_reqh_context_invariant(rctx));

		for (idx = 0; idx < rctx->rc_enr; ++idx) {
			struct c2_net_domain *ndom;

			xprt = cs_xprt_lookup(rctx->rc_eps[idx].xprt, xprts,
								xprts_nr);
			C2_ASSERT(xprt != NULL);

			rc = c2_net_xprt_init(xprt);
			if (rc != 0)
				return rc;

			C2_ALLOC_PTR(ndom);
			if (ndom == NULL)
				return -ENOMEM;

			rc = c2_net_domain_init(ndom, xprt);
			if (rc != 0) {
				c2_free(ndom);
				return rc;
			}
			c2_tlink_init(&ndoms_descr, ndom);
			c2_tlist_add_tail(&ndoms_descr, &cctx->cc_ndoms, ndom);
		}
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

	xprts = cctx->cc_xprts;
	c2_tlist_for(&ndoms_descr, &cctx->cc_ndoms, ndom) {
		c2_net_domain_fini(ndom);
		c2_tlist_del(&ndoms_descr, ndom);
		c2_tlink_fini(&ndoms_descr, ndom);
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
static int cs_start_request_handler(struct cs_reqh_context *rctx)
{
	int                      rc;
	struct c2_cs_reqh_stobs *rstob;
	struct c2_stob_domain   *sdom;

	if (rctx->rc_dbpath != NULL)
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
	if (strcasecmp(rstob->stype, cs_stobs[AD_STOB]) == 0)
		sdom = rstob->adstob;
	else
		sdom = rstob->linuxstob;

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
static int cs_start_request_handlers(struct c2_colibri *cctx)
{
	int                     rc;
	struct cs_reqh_context *rctx;
	FILE                   *ofd;

	C2_PRE(cctx != NULL);
        C2_ASSERT(!c2_tlist_is_empty(&rctx_descr, &cctx->cc_reqh_ctxs));

	ofd = cctx->cc_outfile;
        c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {

		C2_ASSERT(cs_reqh_context_invariant(rctx));

		rc = cs_start_request_handler(rctx);
		if (rc != 0) {
			fprintf(ofd,
				"COLIBRI: Failed to start request handler\n");
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
	cs_rpcmachines_fini(reqh);
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

	c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {
		if (rctx->rc_state == RC_INITIALISED)
			cs_request_handler_stop(rctx);
		cs_reqh_ctx_free(rctx);
	} c2_tlist_endfor;
}

/**
   Initialises a colibri context.

   @param cctx Colibri context to be initialised

   @pre cctx != NULL
 */
static int cs_colibri_init(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);

	c2_tlist_init(&ndoms_descr, &cctx->cc_ndoms);
	c2_tlist_init(&rctx_descr, &cctx->cc_reqh_ctxs);

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

	cs_net_domains_fini(cctx);
	c2_tlist_fini(&ndoms_descr, &cctx->cc_ndoms);
	c2_tlist_fini(&rctx_descr, &cctx->cc_reqh_ctxs);
}

/**
   Displays usage of colibri_setup program.

   @param f File to which the output is written
 */
static void cs_usage(FILE *out)
{
	C2_PRE(out != NULL);

	fprintf(out, "Usage: colibri_setup [-h] [-x] [-l]\n"
		   "    or colibri_setup {-r -T stobtype -D dbpath"
		   " -S stobfile {-e xport:endpoint}+\n"
		   "                     { -s service}+ }\n");
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
		   "   Currently supported transport is bulk-sunrpc which "
		   "takes 3-tuple endpoint\n   address."
		   " e.g. bulk-sunrpc:127.0.0.1:1024:1\n"
		   "   This can be specified multiple times, per request "
		   "handler set. Thus there\n   can exist multiple endpoints "
		   "per network transport with different ids,\n"
		   "   i.e. 3rd component of 3-tuple endpoint address in "
		   "this case.\n"
		   "-s Services to be started in given request handler "
		   "context.\n   This can be specified multiple times "
		   "per request handler set.\n"
		   "   e.g. ./colibri -r -T linux -D dbpath -S stobfile\n"
		   "        -e xport:127.0.0.1:1024:1 -s mds\n");
}

static int reqh_ctxs_are_valid(struct c2_colibri *cctx)
{
	int                     rc;
	int                     idx;
	FILE                   *ofd;
	struct cs_reqh_context *rctx;

	C2_PRE(cctx != NULL);

        C2_ASSERT(!c2_tlist_is_empty(&rctx_descr, &cctx->cc_reqh_ctxs));

	ofd = cctx->cc_outfile;
        c2_tlist_for(&rctx_descr, &cctx->cc_reqh_ctxs, rctx) {

                if (!cs_reqh_context_invariant(rctx)) {
                        fprintf(ofd, "COLIBRI: Missing or invalid parameters\n");
			cs_usage(ofd);
                        return -EINVAL;
                }

		if (!stype_is_valid(rctx->rc_stype)) {
                        fprintf(ofd, "COLIBRI: Invalid storage type\n");
			cs_stob_types_list(ofd);
                        return -EINVAL;
                }
		/*
		   Check if all the given end points in a reqh context are
		   valid.
		 */
		for (idx = 0; idx < rctx->rc_enr; ++idx) {
			rc = cs_endpoint_validate(cctx,
					rctx->rc_eps[idx].endpoint,
					rctx->rc_eps[idx].xprt);
			if (rc == -EADDRINUSE)
				fprintf(ofd,
					"COLIBRI: Duplicate end point: %s:%s\n",
					rctx->rc_eps[idx].xprt,
					rctx->rc_eps[idx].endpoint);
			else if (rc == -EINVAL)
				fprintf(ofd,
					"COLIBRI: Invalid endpoint: %s:%s\n",
					rctx->rc_eps[idx].xprt,
					rctx->rc_eps[idx].endpoint);
			if (rc != 0)
				return rc;
		}

		/*
		   Check if the services are registered and are valid in a
		   reqh context are valid.
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
			if (service_is_duplicate(cctx, rctx->rc_services[idx])) {
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
	struct cs_reqh_context *vrctx = NULL;
	FILE                   *ofd;

	C2_PRE(cctx != NULL);

	if (argc <= 1)
		return -EINVAL;;

	ofd = cctx->cc_outfile;
        C2_GETOPTS("colibri_setup", argc, argv,
                C2_VOIDARG('h', "Colibri_setup usage help",
                        LAMBDA(void, (void)
                        {
				cs_help(ofd);
				rc = 1;
                                return;
                        })),
		C2_VOIDARG('x', "List Supported transports",
			LAMBDA(void, (void)
			{
				cs_xprts_list(ofd, cctx->cc_xprts,
						cctx->cc_xprts_nr);
				rc = 1;
				return;
			})),
		C2_VOIDARG('l', "List Supported services",
			LAMBDA(void, (void)
			{
				cs_services_list(ofd);
				rc = 1;
				return;
			})),
                C2_VOIDARG('r', "Start request handler",
                        LAMBDA(void, (void)
                        {
				vrctx = NULL;
				vrctx = cs_reqh_ctx_alloc(cctx);
				if (vrctx == NULL) {
					rc = -ENOMEM;
					return;
				}
				vrctx->rc_snr = 0;
				vrctx->rc_enr = 0;
                        })),
                C2_STRINGARG('T', "Storage domain type",
                        LAMBDA(void, (const char *str)
			{
				if (vrctx == NULL) {
					rc = -EINVAL;
					return;
				}
                                vrctx->rc_stype = str;
			})),
                C2_STRINGARG('D', "Database environment path",
                        LAMBDA(void, (const char *str)
			{
				if (vrctx == NULL) {
					rc = -EINVAL;
					return;
				}
                                vrctx->rc_dbpath = str;
			})),
                C2_STRINGARG('S', "Storage name",
                        LAMBDA(void, (const char *str)
			{
				if (vrctx == NULL) {
					rc = -EINVAL;
					return;
				}
                                vrctx->rc_stpath = str;
			})),
                C2_STRINGARG('e', "Network endpoint, eg:- transport:address",
                        LAMBDA(void, (const char *str)
                        {
				struct cs_endpoint_and_xprt *ep_xprt;
				if (vrctx == NULL) {
					rc = -EINVAL;
					return;
				}
				if (vrctx->rc_enr == vrctx->rc_max_eps) {
					fprintf(ofd, "Too many endpoints\n");
					rc = -E2BIG;
					return;
				}
				ep_xprt = &vrctx->rc_eps[vrctx->rc_enr];
                                rc = ep_and_xprt_get(ep_xprt, str);
				if (rc != 0)
					return;

				C2_CNT_INC(vrctx->rc_enr);
                        })),
                C2_STRINGARG('s', "Services to be configured",
                        LAMBDA(void, (const char *str)
			{
				if (vrctx == NULL) {
					rc = -EINVAL;
					return;
				}
				if (vrctx->rc_snr == vrctx->rc_max_services) {
					fprintf(ofd, "Too many services\n");
					rc = -E2BIG;
					return;
				}
                                vrctx->rc_services[vrctx->rc_snr] = str;
				C2_CNT_INC(vrctx->rc_snr);
                        })));

	return rc;
}

int c2_cs_setup_env(struct c2_colibri *cctx, int argc, char **argv)
{
	int   rc;

	C2_PRE(cctx != NULL);

	rc = cs_parse_args(cctx, argc, argv);
	if (rc < 0) {
		cs_usage(cctx->cc_outfile);
		return rc;
	}

	rc = reqh_ctxs_are_valid(cctx);
	if (rc != 0)
		return rc;

	rc = cs_net_domains_init(cctx);
	if (rc != 0)
		return rc;

	rc = cs_start_request_handlers(cctx);
	if (rc != 0)
		return rc;

	rc = cs_rpcmachines_init(cctx);

	return rc;
}

int c2_cs_start(struct c2_colibri *cctx)
{
	int   rc;

	C2_PRE(cctx != NULL);

	rc = cs_services_init(cctx);
	if (rc != 0)
		fprintf(cctx->cc_outfile,
			"COLIBRI: Service initialisation failed\n");

	return rc;
}

int c2_cs_init(struct c2_colibri *cctx, struct c2_net_xprt **xprts,
	       int xprts_nr, FILE *out)
{
        int rc;

        C2_PRE(cctx != NULL && xprts != NULL && xprts_nr > 0 && out != NULL);

        cctx->cc_xprts = xprts;
	cctx->cc_xprts_nr = xprts_nr;
	cctx->cc_outfile = out;

	rc = cs_colibri_init(cctx);
	C2_ASSERT(rc == 0);

        return c2_processors_init();
}

void c2_cs_fini(struct c2_colibri *cctx)
{
	C2_PRE(cctx != NULL);

        cs_request_handlers_stop(cctx);
        cs_colibri_fini(cctx);
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
