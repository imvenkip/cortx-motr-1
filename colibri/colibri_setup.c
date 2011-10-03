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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <err.h>
#include <errno.h>     /* errno */
#include <string.h>
#include <signal.h>

#include "lib/misc.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/getopts.h"
#include "lib/bitmap.h"
#include "lib/time.h"
#include "lib/chan.h"
#include "lib/arith.h"
#include "lib/processor.h"

#include "colibri/init.h"
#include "stob/stob.h"
#include "stob/ad.h"
#include "fop/fop.h"
#include "stob/linux.h"
#include "net/net.h"
#include "net/bulk_sunrpc.h"
#include "rpc/rpccore.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "colibri/colibri_setup.h"

/**
   @addtogroup colibri_setup
   @{
 */

/**
   Represents cob domain id, it is incremented
   for every new cob domain.

   @todo Have a generic mechanism to generate
	unique cob domain id.
 */
static int cdom_id;

/**
   Magic used to check consistency of reqh_context object
 */
enum {
	/* Hex conversion of string "reqhctx" */
	REQH_CONTEXT_MAGIC = 0x72657168637478
};

enum {
	CS_MAX_BLOCK_CNT = 500000
};

/**
   Internal structure which encapsulates stob type and
   stob domain references for linux and ad stobs respectively.
 */
struct reqh_stobs {
	/**
	   Type of storage domain to be initialised
	   (e.g. Linux or AD)
	 */
	const char            *stype;
	/**
	   Backend storage object id
	 */
	struct c2_stob_id      stob_id;
	/**
	   Linux storage domain type.
	 */
	struct c2_stob_domain *linuxstob;
	/**
	   Allocation data storage domain type.
	 */
	struct c2_stob_domain *adstob;
};

/**
   Represents state of a request handler context.
 */
enum {
        /**
	   A request handler context is in RC_UNINTIALISED state
	   when it is allocated and added to the list of the same
	   in c2_colibri.
	 */
	RC_UNINITIALISED,
	/**
	   A request handler context is in Initialised state
	   once the request handler (embedded inside the context) is
	   successfully initialised.
	 */
	RC_INITIALISED
};

/**
   Represents a request handler environment.
   It contains configuration information about the various
   global entities to be configured and their corresponding
   instances that are needed to be initialised before the request
   handler is started, which by itself is contained in the same
   structure.
 */
struct reqh_context {
	/** Storage path for request handler context */
	const char               *rc_stpath;
	/** Type of storage to be initialised */
	const char               *rc_stype;
	/** Database environment path for request handler context*/
	const char               *rc_dbpath;
	/** Services running in request handler context */
	const char              **rc_services;
	/** Number services in request handler context */
	int                       rc_snr;
	/**
	    Maximum number of services allowed per request handler
	    context.
	 */
	int                       rc_max_services;
	/** Endpoints configured per request handelr context */
	char                    **rc_endpoints;
	/** Number of endpoints configured per request handler context */
	int                       rc_enr;
	/**
	    Maximum number of endpoints allowed per request handler
	    context.
	 */
	int                       rc_max_endpoints;
	/**
	    State of a request handler context.
	    i.e. RC_INITIALISED or RC_UNINTIALISED.
	 */
	int                       rc_state;
	/** Storage domain for a request handler */
	struct reqh_stobs         rc_stob;
	/** Database used by the request handler */
	struct c2_dbenv           rc_db;
	/** Cob domain to be used by the request handler */
	struct c2_cob_domain      rc_cdom;
	struct c2_cob_domain_id   rc_cdom_id;
	/** File operation log for a request handler */
	struct c2_fol             rc_fol;
	/** Request handler instance to be initialised */
	struct c2_reqh            rc_reqh;
	/** Reqh context magic */
	uint64_t                  rc_magic;
	/** Linkage into reqh context list */
	struct c2_list_link       rc_linkage;
};

/**
   Currently supported stob types in colibri context.
 */
static const char *linux_stob = "Linux";
static const char *ad_stob = "AD";

static struct c2_net_domain *cs_net_domain_locate(struct c2_colibri *cs_colibri,
							const char *xprt);
/**
   Looks up if given xprt is supported in a colibri
   context.

   @param ep End point address for which the transport is to
		be located
   @param xprts Array of network transports supported in a
		colibri environment
   @param xprts_nr Size of xprts array

   @pre xprt_name != NULL && xprts != NULL && xprts_nr > 0

 */
static struct c2_net_xprt *lookup_xprt(const char *ep, struct c2_net_xprt **xprts,
								int xprts_nr)
{
        int   i;
	char *nep;
	int   nep_len;
	int   xprt_len;

	C2_PRE(ep != NULL && xprts != NULL && xprts_nr > 0);

	nep = strchr(ep, ':');
	if (nep == NULL)
		return NULL;

	nep_len = strlen(nep);
	xprt_len = strlen(ep) - nep_len;
        for (i = 0; i < xprts_nr; ++i)
                if (strncmp(ep, xprts[i]->nx_name, xprt_len) == 0)
                        return xprts[i];
        return NULL;
}

static void list_xprts(FILE *f, struct c2_net_xprt **xprts)
{
        int i;

	C2_PRE(f != NULL && xprts != NULL);

        fprintf(f, "\nSupported transports:\n");
        for (i = 0; i < ARRAY_SIZE(xprts); ++i)
                fprintf(f, "    %s\n", xprts[i]->nx_name);
}

/**
   Checks if the specified storage type is supported
   in a colibri context.

   @param stype Storage type

   @pre stype != NULL

   @retval true If storage type is supported
	false If storage type does not supported
 */
static bool stype_is_valid(const char *stype)
{
	C2_PRE(stype != NULL);

	return  ((strcasecmp(stype, ad_stob) == 0 ||
		strcasecmp(stype, linux_stob) == 0));
}

/**
   Lists supported services.

   @pre f != NULL
 */
static void list_services(FILE *f)
{
	struct c2_list *services;
	struct c2_reqh_service_type *stype;
	struct c2_reqh_service_type *stype_next;

	C2_PRE(f != NULL);

	services = c2_reqh_service_list_get();

	fprintf(f, "\n Supported services:\n");
	c2_list_for_each_entry_safe(services, stype, stype_next,
		struct c2_reqh_service_type, rst_linkage) {
		fprintf(f, " %s\n", stype->rst_name);
	}
}

/**
   Checks if given network layer end point address
   is already in use.

   @param cs_colibri Colibri context
   @param xprt Network transport
   @param nep  Network layer end point

   @pre cs_colibri != NULL && xprt != NULL && nep != NULL

   @retval -ENOENT If endpoint is not in use
	-EADDRINUSE If endpoint is in use
 */
static int endpoint_is_inuse(struct c2_colibri *cs_colibri,
				struct c2_net_xprt *xprt, char *nep)
{
	int                        rc;
	struct c2_net_domain      *ndom;
	struct c2_net_transfer_mc *tm;
	struct c2_net_transfer_mc *tm_next;

	C2_PRE(cs_colibri != NULL && xprt != NULL && nep != NULL);

	ndom = cs_net_domain_locate(cs_colibri, xprt->nx_name);
	if (ndom == NULL) {
		rc = -ENOENT;
		goto out;
	}

	rc = 0;
	c2_list_for_each_entry_safe(&ndom->nd_tms, tm, tm_next,
			struct c2_net_transfer_mc, ntm_dom_linkage) {

		if (strcmp(nep, tm->ntm_ep->nep_addr) == 0) {
			rc = -EADDRINUSE;
			break;
		}
	}
out:
	return rc;
}

/**
   Checks if the specified end point address contains valid
   network transport and net layer end point address.
   End point address consists of 2 parts i.e. xport:network layer
   end point.

   @param cs_colibri Colibri context
   @param ep Endpoint address to be validated

   @pre xprts != NULL && ep != NULL

   @retval 0 On success
	-EINVAL On failure
*/
static int ep_is_valid(struct c2_colibri *cs_colibri, char *ep)
{
	int                 rc;
	char               *nep;
	struct c2_net_xprt *xprt;

	C2_PRE(cs_colibri != NULL && ep != NULL);

	xprt = lookup_xprt(ep, cs_colibri->cc_xprts, cs_colibri->cc_xprts_nr);
	if (xprt == NULL) {
		rc = -EINVAL;
		goto out;
	}

	nep = strchr(ep, ':');
	if (nep == NULL) {
		rc = -EINVAL;
		goto out;
	}

	/*
	   strchr lands the pointer at the first occurance of ':'
	   in the endpoint string. So increment the pointer by 1
	   to get the exact network endpoint address.
	 */
	nep++;
	rc = endpoint_is_inuse(cs_colibri, xprt, nep);
	if (rc == -ENOENT)
		rc = 0;
out:
	return rc;
}

/**
   Checks consistency of request handler contex
 */
static bool cs_reqh_ctx_invariant(const struct reqh_context *rctx)
{
	if (rctx == NULL)
		return false;
	switch (rctx->rc_state) {
	case RC_UNINITIALISED:
		return rctx->rc_stype != NULL && stype_is_valid(rctx->rc_stype) &&
			rctx->rc_dbpath != NULL && rctx->rc_stpath != NULL &&
			rctx->rc_enr != 0 && rctx->rc_snr != 0;
	case RC_INITIALISED:
		return rctx->rc_magic == REQH_CONTEXT_MAGIC &&
			c2_reqh_invariant(&rctx->rc_reqh);
	default:
		return false;
	}
}

/**
   Allocates a request handler and adds it to the list of
   the same in given colibri context.

   @param cs_colibri Colibri context

   @see c2_colibri
 */
static struct reqh_context *cs_reqh_ctx_alloc(struct c2_colibri *cs_colibri)
{
	struct reqh_context *rctx = NULL;

	C2_PRE(cs_colibri != NULL);

	C2_ALLOC_PTR(rctx);
	if (rctx == NULL)
		goto out;

	rctx->rc_max_endpoints = cs_colibri->cc_xprts_nr;
	C2_ALLOC_ARR(rctx->rc_endpoints, rctx->rc_max_endpoints);
	if (rctx->rc_endpoints == NULL)
		goto cleanup_rctx;

	rctx->rc_max_services = c2_list_length(c2_reqh_service_list_get());
	if (rctx->rc_max_services == 0)
		goto cleanup_endpoints;

	C2_ALLOC_ARR(rctx->rc_services, rctx->rc_max_services);
	if (rctx->rc_services == NULL)
		goto cleanup_endpoints;

	rctx->rc_magic = REQH_CONTEXT_MAGIC;
	c2_list_add(&cs_colibri->cc_reqh_ctxs, &rctx->rc_linkage);

	goto out;

cleanup_endpoints:
	c2_free(rctx->rc_endpoints);
cleanup_rctx:
	c2_free(rctx);
	rctx = NULL;
out:
	return rctx;
}

static void cs_reqh_ctx_free(struct reqh_context *rctx)
{
	C2_PRE(rctx != NULL);

	c2_free(rctx->rc_endpoints);
	c2_free(rctx->rc_services);
	c2_list_del(&rctx->rc_linkage);
	c2_free(rctx);
}

/**
   Finds network domain for specified network transport in a
   given colibri context.

   @param cs_colibri Colibri context
   @param xprt_name Type of network transport to be initialised

   @pre cs_colibri != NULL && xprt_name != NULL

   @post strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0

   @see c2_cs_init()
 */
static struct c2_net_domain *cs_net_domain_locate(struct c2_colibri *cs_colibri,
							const char *xprt_name)
{
	struct c2_net_domain *ndom = NULL ;
	struct c2_net_domain *ndom_next = NULL;
	bool                  found = false;

	C2_PRE(cs_colibri != NULL && xprt_name != NULL);

	c2_list_for_each_entry_safe(&cs_colibri->cc_ndoms, ndom, ndom_next,
			struct c2_net_domain, nd_app_linkage) {

			if (strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0) {
				found = true;
				break;
			}
	}

	if (found)
		C2_POST(strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0);
	else
		ndom = NULL;

	return ndom;
}

/**
   Initialises rpc machine for the given endpoint.
   Once the new rpcmachine is created it is added to
   list of rpc machines in the given request handler.
   Given request handler should already be initialised
   before this function is invoked.

   @param cs_colibri Colibri context
   @param ep 2 part end point address comprising
	of network transport:network layer endpoint
	address
   @param reqh Request handler to which the newly created
		rpcmachine belongs

   @pre cs_colibri != NULL && ep != NULL && reqh != NULL

   @retval 0 On success
	-errno On failure
 */
static int cs_rpcmachine_init(struct c2_colibri *cs_colibri, char *ep,
						struct c2_reqh *reqh)
{
	struct c2_rpcmachine         *rpcmach;
	struct c2_net_domain         *ndom;
	struct c2_net_xprt           *xprt;
	char                         *nep;
	int                           rc;

	C2_PRE(cs_colibri != NULL && ep != NULL && reqh != NULL);

	xprt = lookup_xprt(ep, cs_colibri->cc_xprts, cs_colibri->cc_xprts_nr);

	if (xprt == NULL)
		return - EINVAL;

	ndom = cs_net_domain_locate(cs_colibri, xprt->nx_name);
	if (ndom == NULL)
		return -EINVAL;

	C2_ALLOC_PTR(rpcmach);
	if (rpcmach == NULL)
		return -ENOMEM;

	nep = strchr(ep, ':');
	nep++;
	rc = c2_rpcmachine_init(rpcmach, reqh->rh_cob_domain, ndom, nep, reqh);
	if (rc != 0) {
		c2_free(rpcmach);
		goto rpm_fail;
	}

	c2_list_add_tail(&reqh->rh_rpcmachines, &rpcmach->cr_rh_linkage);

rpm_fail:
	return rc;
}

/**
   Intialises rpc machines for given end point addresses
   in a colibri context.

   @param cs_colibri Colibri context

   @retval 0 On success
	-errno On failure
 */
static int cs_rpcmachines_init(struct c2_colibri *cs_colibri)
{
	int                   idx;
	int                   rc;
	FILE                 *outfile;
	struct  reqh_context *rctx;
	struct  reqh_context *rctx_next;

	C2_PRE(cs_colibri != NULL);

	outfile = cs_colibri->cc_outfile;
        c2_list_for_each_entry_safe(&cs_colibri->cc_reqh_ctxs, rctx,
				rctx_next, struct reqh_context, rc_linkage) {

		if (!cs_reqh_ctx_invariant(rctx)) {
			fprintf(outfile, "COLIBRI: Invalid input");
			rc = -EINVAL;
			goto out;
		}
		for (idx = 0; idx < rctx->rc_enr; ++idx) {
			rc = ep_is_valid(cs_colibri,
                                        rctx->rc_endpoints[idx]);
			if (rc != 0) {
				if (rc == -EADDRINUSE)
					fprintf(outfile,
					"COLIBRI: Duplicate end point: %s",
						rctx->rc_endpoints[idx]);
				goto out;
			}
			rc = cs_rpcmachine_init(cs_colibri,
						rctx->rc_endpoints[idx],
						&rctx->rc_reqh);
			if (rc != 0)
				goto out;
		}
	}
out:
	return rc;
}

/**
   Finalises all the rpc machines from the list of
   rpc machines present in c2_reqh.

   @param reqh Request handler of which the rpc machines
	are to be destroyed

   @pre reqh != NULL
 */
static void cs_rpcmachines_fini(struct c2_reqh *reqh)
{
	struct c2_rpcmachine *rpcmach;
	struct c2_rpcmachine *rpcmach_next;

	C2_PRE(reqh != NULL);

	c2_list_for_each_entry_safe(&reqh->rh_rpcmachines, rpcmach,
		rpcmach_next, struct c2_rpcmachine, cr_rh_linkage) {

		C2_ASSERT(rpcmach != NULL);
		c2_rpcmachine_fini(rpcmach);
		c2_list_del(&rpcmach->cr_rh_linkage);
		c2_list_link_fini(&rpcmach->cr_rh_linkage);
		c2_free(rpcmach);
	}
}

/*
  Functions to define ad_balloc_ops operations vector,
  this is needed while defining and initialising ad type
  of stob.
  Operations from ad_balloc_ops are invoked implicitly
  during io operations on ad stob.

  @todo Below ad_balloc_ops are used in the same way as
	done in stob/ut/ad.c, To have a generic and more
	elegant way to do so.
 */

struct cs_balloc {
        c2_bindex_t      csb_next;
        struct ad_balloc csb_ballroom;
};

static struct cs_balloc *b2mock(struct ad_balloc *ballroom)
{
        return container_of(ballroom, struct cs_balloc, csb_ballroom);
}

static int cs_balloc_init(struct ad_balloc *ballroom, struct c2_dbenv *db,
                            uint32_t bshift)
{
        return 0;
}

static void cs_balloc_fini(struct ad_balloc *ballroom)
{
}

/**
   Allocates given number of blocks in an extent.
   This is invoked during io operations on ad stob.
 */
static int cs_balloc_alloc(struct ad_balloc *ballroom, struct c2_dtx *dtx,
                             c2_bcount_t count, struct c2_ext *out)
{
        struct cs_balloc *csb = b2mock(ballroom);
        c2_bcount_t giveout;

        giveout = min64u(count, CS_MAX_BLOCK_CNT);
        out->e_start = csb->csb_next;
        out->e_end   = csb->csb_next + giveout;
        csb->csb_next += giveout + 1;
        return 0;
}

static int cs_balloc_free(struct ad_balloc *ballroom, struct c2_dtx *dtx,
                            struct c2_ext *ext)
{
        return 0;
}

/*
  Operations vector for ad_balloc
*/
static const struct ad_balloc_ops cs_balloc_ops = {
        .bo_init  = cs_balloc_init,
        .bo_fini  = cs_balloc_fini,
        .bo_alloc = cs_balloc_alloc,
        .bo_free  = cs_balloc_free,
};

struct cs_balloc csb = {
        .csb_next = 0,
        .csb_ballroom = {
                .ab_ops = &cs_balloc_ops
        }
};

/**
   Initialises storage including database environment and
   stob domain of given type (e.g. linux or ad).
   There is a stob domain and a database environment created
   per request handler context.

   @param stob_type Type of stob to be initialised (e.g. linux or ad)
   @param stob_path Path at which storage object should be created
   @param stob Pre allocated struct reqh_stob_domain wrapper object
	containing c2_stob_domain references for linux and ad stob types
   @param db Pre allocated struct c2_dbenv instance to be initialised

   @see struct reqh_stob_domain

   @pre stob_type != NULL && stob_path != NULL &&
	stob != NULL && db != NULL

   @retval 0 On success
	-errno On failure

   @todo Use generic mechanism to generate stob ids
 */
static int cs_storage_init(const char *stob_type, const char *stob_path,
		struct reqh_stobs *stob, struct c2_dbenv *db)
{
	int                      rc;
	int                      slen;
	char                    *objpath;
        struct c2_stob          *bstore;

	C2_PRE(stob_type != NULL && stob_path != NULL &&
				stob != NULL && db != NULL);

	stob->stype = stob_type;

	/*
	   XXX Need generic mechanism to generate stob ids
	 */
        stob->stob_id.si_bits.u_hi = 0x0;
        stob->stob_id.si_bits.u_lo = 0xdf11e;

	slen = strlen(stob_path);
	C2_ALLOC_ARR(objpath, slen + sizeof("/o"));
	if (objpath == NULL)
		return -ENOMEM;

	sprintf(objpath, "%s%s", stob_path, "/o");

	rc = mkdir(stob_path, 0700);
        if (rc != 0 && (rc != -1 && errno != EEXIST))
		goto out;

        rc = mkdir(objpath, 0700);
        if (rc != 0 && (rc != -1 && errno != EEXIST))
		goto out;

	rc = linux_stob_type.st_op->sto_domain_locate(&linux_stob_type,
					stob_path, &stob->linuxstob);

	if  (rc != 0)
		goto out;

	rc = stob->linuxstob->sd_ops->sdo_stob_find(stob->linuxstob,
						&stob->stob_id, &bstore);
	if  (rc != 0)
		goto out;

	C2_ASSERT(bstore->so_state == CSS_UNKNOWN);

	if  (rc != 0)
		goto out;

	rc = c2_stob_create(bstore, NULL);
	if (rc != 0)
		goto out;
	C2_ASSERT(bstore->so_state == CSS_EXISTS);

	if (strcasecmp(stob_type, ad_stob) == 0) {
		rc = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type,
					"adstob", &stob->adstob);
		if (rc != 0)
			goto out;

		rc = c2_ad_stob_setup(stob->adstob, db, bstore,
						&csb.csb_ballroom);
		if (rc != 0)
			goto out;
	}

out:
	if (bstore != NULL)
		c2_stob_put(bstore);
	c2_free(objpath);

	return rc;
}

/**
   Finalises storage for a request handler in a colibri
   context.

   @param stob Wrapper stob containing c2_stob_domain
		references for linux and ad stobs to be
		finalised

   @see struct reqh_stob_domain

   @pre stob != NULL
 */
static void cs_storage_fini(struct reqh_stobs *stob)
{
	C2_PRE(stob != NULL);

	if (stob->linuxstob != NULL || stob->adstob != NULL) {
		if (strcasecmp(stob->stype, ad_stob) == 0)
			stob->adstob->sd_ops->sdo_fini(stob->adstob);
		stob->linuxstob->sd_ops->sdo_fini(stob->linuxstob);
	}
}

/**
   Initialises and registers a particular type of service with
   the given request handler in colibri context.
   Once the service is initialised it is started, and registered
   with the appropriate request handler.

   @param service_name Name of service to be initialised
   @param reqh Request handler this service is registered with

   @pre service_name != NULL && reqh != NULL

   @post c2_reqh_service_invariant(service)
 */
static int cs_service_init(const char *service_name, struct c2_reqh *reqh)
{
	int                          rc;
	struct c2_reqh_service      *service;

	C2_PRE(service_name != NULL && reqh != NULL);

	rc = c2_reqh_service_init(&service, service_name);
	if (rc != 0)
		goto out;

	c2_reqh_service_start(service, reqh);

	C2_POST(c2_reqh_service_invariant(service));

out:
	return rc;
}

/**
   Initialises set of services specified in a given request
   handler context.
   Services are started once the colibri context is configured
   successfuly which includes network domains, request handlers,
   and rpc machines.

   @param cs_colibri Colibri context

   @retval 0 On success
	-errno On failure
 */
static int cs_services_init(struct c2_colibri *cs_colibri)
{
	int                   idx;
	int                   rc;
	struct  reqh_context *rctx;
	struct  reqh_context *rctx_next;
	FILE                 *outfile;

	C2_PRE(cs_colibri != NULL);

	outfile = cs_colibri->cc_outfile;
        c2_list_for_each_entry_safe(&cs_colibri->cc_reqh_ctxs, rctx,
				rctx_next, struct reqh_context, rc_linkage) {
		if (!cs_reqh_ctx_invariant(rctx)) {
			fprintf(outfile, "COLIBRI: Invalid input");
			rc = -EINVAL;
			goto out;
		}

		for (idx = 0; idx < rctx->rc_snr; ++idx) {
			rc = cs_service_init(rctx->rc_services[idx], &rctx->rc_reqh);
			if (rc != 0) {
				fprintf(outfile, "COLIBRI: Invalid service: %s",
							rctx->rc_services[idx]);
				goto out;
			}
		}
	}
out:
	return rc;
}

/**
   Finalises a particular service registered with a request
   handler in colibri environment.
   Stops the service, and unregisters it from its request
   handler.

   @param service Service to be finalised

   @pre service != NULL
 */
static void cs_service_fini(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL);

	c2_reqh_service_stop(service);
	c2_reqh_service_fini(service);
}

/**
   Finalises all the services registered with the given
   request handler.
   Traverses through the services list and invokes
   cs_service_fini() on each individual service.

   @param reqh Request handler of which the services are to
	be finalised

   @pre reqh != NULL
 */
static void cs_services_fini(struct c2_reqh *reqh)
{
	struct c2_reqh_service           *service;
	struct c2_reqh_service           *service_next;
	struct c2_list                   *services;

	C2_PRE(reqh != NULL);

	services = &reqh->rh_services;
        c2_list_for_each_entry_safe(services, service, service_next,
                struct c2_reqh_service, rs_linkage) {

		C2_ASSERT(c2_reqh_service_invariant(service));
		cs_service_fini(service);
	}
}

/**
   Initialises network domains per given distinct xport:endpoint
   pair in a colibri context.

   @param cs_colibri Colibri context

   @retval 0 On success
	-errno On failure
 */
static int cs_net_domains_init(struct c2_colibri *cs_colibri)
{
	int                  idx;
	int                  rc;
	int                  xprts_nr;
	struct c2_net_xprt **xprts;
	struct c2_net_xprt  *xprt;
	FILE                *outfile;
	struct reqh_context *rctx;
	struct reqh_context *rctx_next;

	C2_PRE(cs_colibri != NULL);

	outfile = cs_colibri->cc_outfile;
	xprts = cs_colibri->cc_xprts;
	xprts_nr = cs_colibri->cc_xprts_nr;

        c2_list_for_each_entry_safe(&cs_colibri->cc_reqh_ctxs, rctx,
				rctx_next, struct reqh_context, rc_linkage) {

		if (!cs_reqh_ctx_invariant(rctx)) {
			fprintf(outfile, "COLIBRI: Invalid input");
			rc = -EINVAL;
			goto out;
		}

		for (idx = 0; idx < rctx->rc_enr; ++idx) {
			struct c2_net_domain *ndom;
			rc = ep_is_valid(cs_colibri,
					rctx->rc_endpoints[idx]);
			if (rc == -EINVAL) {
				fprintf(outfile,
					"COLIBRI: Invalid endpoint: %s",
						rctx->rc_endpoints[idx]);
				goto out;
			}
			xprt = lookup_xprt(rctx->rc_endpoints[idx],
						xprts, xprts_nr);
			C2_ASSERT(xprt != NULL);

			C2_ALLOC_PTR(ndom);
			if (ndom == NULL) {
				rc = -ENOMEM;
				goto out;
			}

			rc = c2_net_xprt_init(xprt);
			if (rc != 0)
				goto out;
			rc = c2_net_domain_init(ndom, xprt);
			c2_list_add_tail(&cs_colibri->cc_ndoms,
						&ndom->nd_app_linkage);
		}
	}
out:
	return rc;
}

/**
   Finalises all the network domains within a colibri
   context.

   @param cs_colibri Colibri context to which the network
		domains belong
 */
static void cs_net_domains_fini(struct c2_colibri *cs_colibri)
{
	struct c2_net_domain  *ndom;
	struct c2_net_domain  *ndom_next;
	struct c2_net_xprt   **xprts;
	int                    idx;

	C2_PRE(cs_colibri != NULL);

	xprts = cs_colibri->cc_xprts;
	c2_list_for_each_entry_safe(&cs_colibri->cc_ndoms, ndom, ndom_next,
			struct c2_net_domain, nd_app_linkage) {

		c2_net_domain_fini(ndom);
		c2_list_del(&ndom->nd_app_linkage);
		c2_list_link_fini(&ndom->nd_app_linkage);
		c2_free(ndom);
	}

	for (idx = 0; idx < ARRAY_SIZE(xprts); ++idx)
		c2_net_xprt_fini(xprts[idx]);
}

/**
   Initialises a request handler context.
   A request handler context consists of the storage
   domain, database, cob domain, fol and request handler
   instance to be initialised.
   The request handler context is allocated and initialised
   per request handler in a colibri process per node.
   So, there can exist multiple request handlers and thus
   multiple request handler contexts in a colibri context.

   @param rctx Request handler context to be initialised

   @pre rctx != NULL

   @retval 0 On success
	-errno On failure
 */
static int cs_reqh_ctx_init(struct reqh_context *rctx)
{
	int                      rc;
	struct reqh_stobs       *rstob;
	struct c2_stob_domain   *sdom;

	C2_PRE(rctx != NULL);

	if (rctx->rc_dbpath != NULL)
		rc = c2_dbenv_init(&rctx->rc_db, rctx->rc_dbpath, 0);

	if (rc != 0)
		goto out;

	rc = cs_storage_init(rctx->rc_stype, rctx->rc_stpath,
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
	if (strcasecmp(rstob->stype, ad_stob) == 0)
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
	cs_storage_fini(&rctx->rc_stob);
cleanup_db:
	c2_dbenv_fini(&rctx->rc_db);
out:
	return rc;
}

static int cs_reqh_ctxs_init(struct c2_colibri *cs_colibri)
{
	int                     rc;
	struct reqh_context    *rctx;
	struct reqh_context    *rctx_next;
	FILE                   *outfile;

	C2_PRE(cs_colibri != NULL);

	outfile = cs_colibri->cc_outfile;
        c2_list_for_each_entry_safe(&cs_colibri->cc_reqh_ctxs, rctx,
				rctx_next, struct reqh_context, rc_linkage) {

		if (!cs_reqh_ctx_invariant(rctx)) {
			fprintf(outfile, "COLIBRI: Invalid input");
			rc = -EINVAL;
			goto out;
		}

		rc = cs_reqh_ctx_init(rctx);
		if (rc != 0) {
			fprintf(outfile, "COLIBRI: Initialisation failed");
			goto out;
		}
	}
out:
	return rc;
}

/**
   Finalises a request handler context.
   Request handler within the context should be
   finalised before invoking this function.

   @param rctx Request handler context to be finalised

   @pre cs_reqh_ctx_invariant()
 */
static void cs_reqh_ctx_fini(struct reqh_context *rctx)
{
	struct c2_reqh *reqh;

	C2_PRE(cs_reqh_ctx_invariant(rctx));

	reqh = &rctx->rc_reqh;
	cs_rpcmachines_fini(reqh);
	cs_services_fini(reqh);
	c2_reqh_fini(reqh);
	c2_fol_fini(&rctx->rc_fol);
	c2_cob_domain_fini(&rctx->rc_cdom);
	cs_storage_fini(&rctx->rc_stob);
	c2_dbenv_fini(&rctx->rc_db);
}

/**
   Finalises all the request handler contexts within a
   given colibri context.

   @param cs_colibri Colibri context to which the reqeust
		handler contexts belong
 */
static void cs_reqh_ctxs_fini(struct c2_colibri *cs_colibri)
{
	struct reqh_context *rctx;
	struct reqh_context *rctx_next;

	c2_list_for_each_entry_safe(&cs_colibri->cc_reqh_ctxs, rctx,
			rctx_next, struct reqh_context, rc_linkage) {
		if (rctx->rc_state == RC_INITIALISED)
			cs_reqh_ctx_fini(rctx);
		cs_reqh_ctx_free(rctx);
	}
}

/**
   Initialises given colibri context, containing
   network domains for given network transports.

   @param cs_colibri Colibri context to be initialised

   @pre cs_colibri != NULL

   @retval 0 On success
	-errno On failure
 */
static int cs_colibri_init(struct c2_colibri *cs_colibri)
{
	int rc;

	C2_PRE(cs_colibri != NULL);

	c2_list_init(&cs_colibri->cc_ndoms);
	c2_list_init(&cs_colibri->cc_reqh_ctxs);

	rc = c2_processors_init();

	return rc;
}

/**
   Finalises given colibri context.

   @pre cs_colibri != NULL

   @param cs_colibri Colibri context to be finalised
 */
static void cs_colibri_fini(struct c2_colibri *cs_colibri)
{
	C2_PRE(cs_colibri != NULL);

	cs_net_domains_fini(cs_colibri);
	c2_list_fini(&cs_colibri->cc_ndoms);
	c2_list_fini(&cs_colibri->cc_reqh_ctxs);
	c2_processors_fini();
}

/**
   Displays usage of colibri_setup program.

   @param f File to which the output is written
 */
static void cs_usage(FILE *f)
{
	C2_PRE(f != NULL);

	fprintf(f, "usage: colibri_setup [-h] [-x] [-l]\n"
		   "    or colibri_setup {-r -T stobtype -D dbpath"
		   " -S stobfile -e xport:endpoint -s service}\n");
}

/**
   Displays help for colibri_setup program.

   @param f File to which the output is written
 */
static void cs_help(FILE *f)
{
	C2_PRE(f != NULL);

	cs_usage(f);
	fprintf(f, "Every -r option represents a request handler set.\n"
		   "Thus all the options in a request handler set are "
		   "mandatory.\n"
		   "There can be multiple such request handler sets in a "
		   "single colibri process.\n"
		   "There can be multiple endpoints and services specified\n"
		   "per request handler set. Multiple endpoints and services\n"
		   "can be specified by using -e and -s options multiple\n"
		   "times respectively per request handler set.\n"
		   "-h Prints colibri usage help\n"
		   "   e.g. colibri_setup -h\n"
		   "-x Lists supported network transports\n"
		   "   e.g. colibri_setup -x\n"
		   "-l Lists supported services on this node\n"
		   "   e.g. colibri_setup -l\n"
		   "-r Represents a request handler context\n"
		   "-T Type of storage to be used by\n"
                   "   the request handler in current context,\n"
		   "   This is specified once per request handler\n"
		   "   context e.g. linux, ad\n"
		   "-D Database file to be used in a request handler\n"
		   "   This is specified once per request handler set\n"
		   "-S Stob file for request handler context\n"
		   "   This is specified once per request handler set\n"
		   "-e Network layer endpoint to which clients\n"
		   "   connect.Network layer endpoint consists of 2 parts\n"
		   "   network transport:endpoint address.\n"
		   "   Currently supported transport is bulk-sunrpc\n"
		   "   which takes 3-tuple endpoint address\n"
		   "   e.g. bulk-sunrpc:127.0.0.1:1024:1\n"
		   "   This can be specified multiple times, per request\n"
		   "   handler set. Thus there can exist multiple endpoints\n"
		   "   per network transport with different ids, i.e. 3rd\n"
		   "   component of 3-tuple endpoint address in this case.\n"
		   "-s Services to be started in given request handler\n"
		   "   context This can be specified multiple times per request\n"
		   "   handler set\n"
		   "   e.g. ./colibri -r -T linux -D dbpath -S stobfile\n"
		   "        -e xport:127.0.0.1:1024:1 -s mds\n");
}

/**
   Parses given arguments and allocates request handler context,
   If all the required arguments are provided and valid.
   Every allocated request handler context is added to the list
   of the same in given colibri context.

   @param cs_colibri Colibri context to be setup

   @retval 0 On success
	-errno On failure
 */
static int cs_parse_args(struct c2_colibri *cs_colibri, int argc,
						char **argv)
{
	int                     rc = 0;
	struct reqh_context    *vrctx = NULL;
	FILE                   *outfile;

	C2_PRE(cs_colibri != NULL);

	if (argc <= 1)
		goto out;

	outfile = cs_colibri->cc_outfile;
        C2_GETOPTS("colibri_setup", argc, argv,
                C2_VOIDARG('h', "Colibri_setup usage help",
                        LAMBDA(void, (void)
                        {
				cs_help(outfile);
				rc = 1;
                                return;
                        })),
		C2_VOIDARG('x', "List Supported transports",
			LAMBDA(void, (void)
			{
				list_xprts(outfile, cs_colibri->cc_xprts);
				rc = 1;
				return;
			})),
		C2_VOIDARG('l', "List Supported services",
			LAMBDA(void, (void)
			{
				list_services(outfile);
				rc = 1;
				return;
			})),
                C2_VOIDARG('r', "Start request handler",
                        LAMBDA(void, (void)
                        {
				vrctx = NULL;
				vrctx = cs_reqh_ctx_alloc(cs_colibri);
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
				if (vrctx == NULL)
					return;
                                vrctx->rc_stype = str;
			})),
                C2_STRINGARG('D', "Database environment path",
                        LAMBDA(void, (const char *str)
			{
				if (vrctx == NULL)
					return;
                                vrctx->rc_dbpath = str;
			})),
                C2_STRINGARG('S', "Storage name",
                        LAMBDA(void, (const char *str)
			{
				if (vrctx == NULL)
					return;
                                vrctx->rc_stpath = str;
			})),
                C2_STRINGARG('e', "Network endpoint, eg:- transport:address",
                        LAMBDA(void, (const char *str)
                        {
				if (vrctx == NULL)
					return;
				if (vrctx->rc_enr == vrctx->rc_max_endpoints) {
					fprintf(outfile, "Too many endpoints");
					rc = -E2BIG;
					return;
				}
                                vrctx->rc_endpoints[vrctx->rc_enr] = (char *)str;
				C2_CNT_INC(vrctx->rc_enr);
                        })),
                C2_STRINGARG('s', "Services to be configured",
                        LAMBDA(void, (const char *str)
			{
				if (vrctx == NULL)
					return;
				if (vrctx->rc_snr == vrctx->rc_max_services) {
					fprintf(outfile, "Too many services");
					rc = -E2BIG;
					return;
				}
                                vrctx->rc_services[vrctx->rc_snr] = str;
				C2_CNT_INC(vrctx->rc_snr);
                        })));
out:
	if (vrctx == NULL)
		rc = -EINVAL;

	return rc;
}

int c2_cs_setup_env(struct c2_colibri *cs_colibri, int argc, char **argv)
{
	int                     rc;
	FILE                   *outfile;

	C2_PRE(cs_colibri != NULL);

	outfile = cs_colibri->cc_outfile;

	rc = cs_parse_args(cs_colibri, argc, argv);
	if (rc != 0)
		goto out;

	rc = cs_net_domains_init(cs_colibri);
	if (rc != 0)
		goto out;

	rc = cs_reqh_ctxs_init(cs_colibri);
	if (rc != 0)
		goto out;

	rc = cs_rpcmachines_init(cs_colibri);
out:
	return rc;
}

int c2_cs_start(struct c2_colibri *cs_colibri)
{
	int   rc;
	FILE *outfile;

	C2_PRE(cs_colibri != NULL);

	outfile = cs_colibri->cc_outfile;
	rc = cs_services_init(cs_colibri);
	if (rc != 0)
		list_services(outfile);

	return rc;
}

int c2_cs_init(struct c2_colibri *cs_colibri,
		struct c2_net_xprt **xprts, int xprts_nr, FILE *out)
{
        int rc;

        C2_PRE(cs_colibri != NULL && xprts != NULL && xprts_nr > 0
						&& out != NULL);

        cs_colibri->cc_xprts = xprts;
	cs_colibri->cc_xprts_nr = xprts_nr;

	cs_colibri->cc_outfile = out;

        rc = c2_init();

        if (rc == 0) {
                rc = cs_colibri_init(cs_colibri);
                if (rc != 0)
                        c2_fini();
        }

        return rc;
}

void c2_cs_fini(struct c2_colibri *cs_colibri)
{
	C2_PRE(cs_colibri != NULL);

        cs_reqh_ctxs_fini(cs_colibri);
        cs_colibri_fini(cs_colibri);
        c2_fini();
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
