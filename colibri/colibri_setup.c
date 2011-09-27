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
#include <unistd.h>    /* pause */
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
#include "net/bulk_mem.h"
#include "net/bulk_sunrpc.h"
#include "rpc/rpccore.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"

/**
   @defgroup colibri_setup Configures user space colibri process

   Colibri setup program configures and starts a user space colibri
   process on a node in a cluster.
   There exists a list of network transports supported by a node,
   which is use to initialise corresponding network domains per colibri
   process, So there exists a network domain per network transport.
   There can exists multiple request handlers per colibri process.
   Every colibri process configures one or more request handler
   contexts, one per request handler, each containing a storage domain,
   data base, cob domain, fol and request handler to be initialised.
   Every request handler contains list of rpc machines, each configured
   per given endpoint per network domain.
   Network domains are shared between multiple request handlers in a
   colibri process.
   There exists multiple services within a colibri process.
   Each service identifies a particular set of operations that can be
   executed on a particular node.
   Services are registered with the request handler which performs the
   execution of requests directed to a particular service. Thus the
   services run under request handler context.

   @{
 */

/*
   Basic test code representing configuration of a service
 */
void mds_start(struct c2_reqh_service *service,
			struct c2_reqh *reqh);
void mds_stop(struct c2_reqh_service *service);
int mds_init(struct c2_reqh_service **service,
		struct c2_reqh_service_type *stype);
void mds_fini(struct c2_reqh_service *service);

struct c2_reqh_service_type mds_type;

struct c2_reqh_service_type_ops mds_type_ops = {
        .rsto_service_init = mds_init
};

struct c2_reqh_service_ops mds_ops = {
	.rso_start = mds_start,
	.rso_stop = mds_stop,
        .rso_fini = mds_fini
};

int mds_init (struct c2_reqh_service **service,
			struct c2_reqh_service_type *stype)
{
	struct c2_reqh_service      *serv;
        int                          rc;

        C2_PRE(service != NULL && stype != NULL);

        printf("\n Initialising mds service \n");
        C2_ALLOC_PTR(serv);
        if (serv == NULL)
                return -ENOMEM;

	serv->rs_type = stype;
        serv->rs_ops = &mds_ops;

	rc = c2_reqh_service_init(serv, stype->rst_name);

	if (rc != 0)
		*service = NULL;

	*service = serv;

        return rc;
}

void mds_start(struct c2_reqh_service *service, struct c2_reqh *reqh)
{
	C2_PRE(service != NULL);

	printf("\n Starting mds.. \n");
	/*
	   Can perform service specific initialisation of
	   objects like fops.
	 */
	c2_reqh_service_start(service, reqh);
}

void mds_stop(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL);

	printf("\n Stopping mds.. \n");
	/*
	   Can finalise service specific objects like
	   fops.
	 */
	c2_reqh_service_stop(service);
}

void mds_fini(struct c2_reqh_service *service)
{
        printf("\n finalizing service \n");
	c2_reqh_service_fini(service);
	c2_free(service);
}
/* Test code ends */

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

struct colibri {
        /**
           List of request handlers running under
	   one colibri process address space on a node.
         */
        struct c2_list            c_reqhs;
        /**
           List of network domain per colibri process
	   address space.
         */
        struct c2_list            c_ndoms;
};

/**
   Represents various network transports supported
   by a particular node in a cluster.
 */
static struct c2_net_xprt *cs_xprts [] = {
	&c2_net_bulk_sunrpc_xprt
};

/**
   Encapsulates stob type and stob domain references
   for linux and ad stobs respectively.
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
   Represents a request handler environment.
   This encapsulates various global entities
   that are needed to be initialised before
   the request handler is started, and request
   handler instance itself.
 */
struct reqh_context {
	/** Storage domain for a request handler */
	struct reqh_stobs         rc_stob;
	/** Database used by the request handle */
	struct c2_dbenv           rc_db;
	/** Cob domain to be used by the request handler */
	struct c2_cob_domain      rc_cdom;
	struct c2_cob_domain_id   rc_cdom_id;
	/** File operation log for a request handler */
	struct c2_fol             rc_fol;
	/** Request handler instance to be initialised */
	struct c2_reqh            rc_reqh;
	/** Reqh contex magic */
	uint64_t                  rc_magic;
};

static struct colibri cs_colibri;

/**
   Checks consistency of request handler contex
 */
bool cs_reqh_ctx_invariant(const struct reqh_context *rctx)
{
	return rctx != NULL && rctx->rc_magic == REQH_CONTEXT_MAGIC;
}

/**
   Locates network domain for given transport and domain.
   All the supported network transports for a node should
   already be registered globally and every respective
   transport should understand the network domain it associates
   to during laters initialisation.

   @param xprt_name Type of network transport to be initialised
   @param domain_name Type of network domain to be initialised

   @pre xprt_name != NULL

   @post strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0
 */
static struct c2_net_domain *cs_net_domain_locate(char *xprt_name)
{
	struct c2_net_domain *ndom = NULL ;
	struct c2_net_domain *ndom_next = NULL;

	C2_PRE(xprt_name != NULL);

	c2_list_for_each_entry_safe(&cs_colibri.c_ndoms, ndom, ndom_next,
			struct c2_net_domain, nd_col_linkage) {

			if (strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0)
				break;
	}

	if (ndom != NULL)
		C2_POST(strcmp(ndom->nd_xprt->nx_name, xprt_name) == 0);

	return ndom;
}

/**
   Initialises rpc machine for the given endpoint.
   Using the rh_index, request handler instance is
   located from the global list of request handlers,
   in cs_colibri.
   Once the new rpcmachine is created it is added to
   list of rpc machines in the request handler.

   @param ep 2 part end point address comprising
	of network transport:3-tuple network layer
	endpoint address

   @param rh_index Index of request handler in the
	list of request handlers in cs_colibri. Using
	this appropriate request handler is located to
	which newly created rpcmachine is addded

   @see struct colibri

   @pre ep != NULL && rh_index > 0

   @retval 0 On success
	-errno On failure
 */
static int cs_rpcmachine_init(char *ep, int rh_index)
{
	struct c2_rpcmachine         *rpcmach;
	char                         *xprt_name;
	struct c2_net_domain         *ndom;
	struct c2_reqh               *reqh;
	struct c2_reqh               *reqh_next;
	char                         *epaddr;
	int                           rc;
	int                           rindex;

	C2_PRE(ep != NULL && rh_index > 0);

	printf("\n %s \n", ep);
	xprt_name = strtok(ep,":");
	epaddr = strtok(NULL, " ");

	C2_ASSERT(xprt_name != NULL);
	C2_ASSERT(epaddr != NULL);

	ndom = cs_net_domain_locate(xprt_name);
	if (ndom == NULL)
		return -EINVAL;

	C2_ALLOC_PTR(rpcmach);
	if (rpcmach == NULL)
		return -ENOMEM;

	rindex = 1;
	c2_list_for_each_entry_safe(&cs_colibri.c_reqhs, reqh, reqh_next,
			struct c2_reqh, rh_colibri_linkage) {
		if (rindex == rh_index)
			break;

		++rindex;
	}

	rc = c2_rpcmachine_init(rpcmach, reqh->rh_cob_domain, ndom, epaddr, reqh);
	if (rc != 0)
		goto rpm_fail;

	c2_list_link_init(&rpcmach->cr_rh_linkage);
	c2_list_add_tail(&reqh->rh_rpcmachines, &rpcmach->cr_rh_linkage);

rpm_fail:
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

struct mock_balloc {
        c2_bindex_t      mb_next;
        struct ad_balloc mb_ballroom;
};

static struct mock_balloc *b2mock(struct ad_balloc *ballroom)
{
        return container_of(ballroom, struct mock_balloc, mb_ballroom);
}

static int mock_balloc_init(struct ad_balloc *ballroom, struct c2_dbenv *db,
                            uint32_t bshift)
{
        return 0;
}

static void mock_balloc_fini(struct ad_balloc *ballroom)
{
}

/**
   Allocates given number of blocks in an extent.
   This is invoked during io operations on ad stob.
 */
static int mock_balloc_alloc(struct ad_balloc *ballroom, struct c2_dtx *dtx,
                             c2_bcount_t count, struct c2_ext *out)
{
        struct mock_balloc *mb = b2mock(ballroom);
        c2_bcount_t giveout;

        giveout = min64u(count, 500000);
        out->e_start = mb->mb_next;
        out->e_end   = mb->mb_next + giveout;
        mb->mb_next += giveout + 1;
        return 0;
}

static int mock_balloc_free(struct ad_balloc *ballroom, struct c2_dtx *dtx,
                            struct c2_ext *ext)
{
        return 0;
}

/*
  Operations vector for ad_balloc
*/
static const struct ad_balloc_ops mock_balloc_ops = {
        .bo_init  = mock_balloc_init,
        .bo_fini  = mock_balloc_fini,
        .bo_alloc = mock_balloc_alloc,
        .bo_free  = mock_balloc_free,
};

struct mock_balloc mb = {
        .mb_next = 0,
        .mb_ballroom = {
                .ab_ops = &mock_balloc_ops
        }
};

/**
   Initialises storage including database environment and
   stob domain of given type (e.g. linux or ad).
   There is a stob domain and a database environment created
   per request handler context.

   @param stob_type Type of stob to be initialised (e.g. linux or ad)
   @param stob_path Path at which storage object should be created
   @param dbpath Path at which database environment should be created
   @param stob Pre allocated struct reqh_stob_domain wrapper object
	containing c2_stob_domain references for linux and ad stob types
   @param db Pre allocated struct c2_dbenv instance to be initialised

   @see struct reqh_stob_domain

   @pre stob_type != NULL && stob_path != NULL &&
	dbpath != NULL && stob != NULL && db != NULL
 */
static int cs_storage_init(const char *stob_type, const char *stob_path,
		const char *dbpath, struct reqh_stobs *stob,
		struct c2_dbenv *db)
{
	int                      rc;
	int                      slen;
	char                    *objpath;
        struct c2_stob          *bstore;
	bool                     cleanup_db = false;

	C2_PRE(stob_type != NULL && stob_path != NULL &&
		dbpath != NULL && stob != NULL && db != NULL);

	stob->stype = stob_type;

	/*
	   XXX Need generic mechanism to generate stob ids
	 */
        stob->stob_id.si_bits.u_hi = 0x0;
        stob->stob_id.si_bits.u_lo = 0xdf11e;

	slen = strlen(stob_path);
	C2_ALLOC_ARR(objpath, slen + strlen("/o") + 1);
	if (objpath == NULL)
		return -ENOMEM;

	sprintf(objpath, "%s%s", stob_path, "/o");

	rc = mkdir(stob_path, 0700);
        if (rc != 0 && (rc != -1 && errno != EEXIST))
		goto out;

        rc = mkdir(dbpath, 0700);
        if (rc != 0 && (rc != -1 && errno != EEXIST))
		goto out;

        rc = mkdir(objpath, 0700);
        if (rc != 0 && (rc != -1 && errno != EEXIST))
		goto out;

	rc = c2_dbenv_init(db, dbpath, 0);

	if  (rc != 0)
		goto out;

	cleanup_db = true;
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

	if (strcmp(stob_type, "AD") == 0 ||
		strcmp(stob_type, "ad") == 0) {
		rc = ad_stob_type.st_op->sto_domain_locate(&ad_stob_type,
					"adstob", &stob->adstob);
		if (rc != 0)
			goto out;

		rc = c2_ad_stob_setup(stob->adstob, db, bstore,
						&mb.mb_ballroom);
		if (rc != 0)
			goto out;

	}

	c2_stob_put(bstore);
	c2_free(objpath);
out:
	if (rc != 0 && cleanup_db)
		c2_dbenv_fini(db);

	return rc;
}

/**
   Finalises storage for a request handler in colibri
   process context.

   @param stob Generic stob containing actual c2_stob_domain
	references to be finalised
   @param db Database to be finalised

   @see struct reqh_stob_domain

   @pre stob != NULL && db != NULL
 */
static void cs_storage_fini(struct reqh_stobs *stob,
				struct c2_dbenv *db)
{
	C2_PRE(stob != NULL && db != NULL);

	if (stob->linuxstob != NULL || stob->adstob != NULL) {
		if (strcmp(stob->stype, "AD") == 0 ||
			strcmp(stob->stype, "ad") == 0)
			stob->adstob->sd_ops->sdo_fini(stob->adstob);
		stob->linuxstob->sd_ops->sdo_fini(stob->linuxstob);
		c2_dbenv_fini(db);
	}
}

/**
   Initialises and registers a particualr type of service with
   a request handler in colibri process contex.
   using rh_index appropriate request handler is located amongst
   the list of request handlers present in global colibri instance.
   once the service is initialised it is started, and registered
   with the appropriate request handler.

   @param service_name Name of service to be initialised
   @param rh_index Index into list of request handlers present in
	global struct colibri instance.

   @pre service_name != NULL && rh_index > 0

   @post c2_reqh_service_invariant(service)

   @see struct colibri
 */
static int cs_service_init(const char *service_name, int rh_index)
{
	int                          rc;
	int                          rindex;
	struct c2_reqh_service_type *stype;
	struct c2_reqh_service      *service;
	struct c2_reqh              *reqh;
	struct c2_reqh              *reqh_next;

	C2_PRE(service_name != NULL && rh_index > 0);

	rindex = 1;
	c2_list_for_each_entry_safe(&cs_colibri.c_reqhs, reqh, reqh_next,
			struct c2_reqh, rh_colibri_linkage) {
		if (rindex == rh_index)
			break;

		++rindex;
	}

	stype = c2_reqh_service_type_find(service_name);
	if (stype == NULL) {
		rc = -EINVAL;
		goto out;
	}
	rc = stype->rst_ops->rsto_service_init(&service, stype);
	if (rc != 0)
		goto out;

	service->rs_ops->rso_start(service, reqh);

	C2_POST(c2_reqh_service_invariant(service));

out:
	return rc;
}

/**
   Finalises a particular service registered with a request
   handler in colibri process context.
   Stops the service, and unregisters it from its request
   handler.

   @param service Service to be finalised

   @pre service != NULL
 */
static void cs_service_fini(struct c2_reqh_service *service)
{
	C2_PRE(service != NULL);

	service->rs_ops->rso_stop(service);
	service->rs_ops->rso_fini(service);
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
   Initialises network domains corresponding to all
   the registered network transports on a node in cluster.
   All the supoprted transports should be pre registered
   in the global array of c2_net_xprt cs_xprts.
   Every registered transport is initialised first and
   corresponding c2_net_domain for that c2_net_xprt is
   allocated and initialised. The c2_net_domain_init()
   operation provided by the corresponding c2_net_xprt
   should  appropriately register the network domain name
   within the network domain,
   As both network transport name and network domain name is
   used to locate the appropriate network domain object from
   the laters list.
   Once the network domain is initialised it is added to the
   domain list in global struct colibri instance.

   @retval 0 On success
	-errno On failure
 */
static int cs_net_domains_init()
{
	int xprt_idx;
	int rc;

	for (xprt_idx = 0; xprt_idx < ARRAY_SIZE(cs_xprts); ++xprt_idx) {
		struct c2_net_domain *ndom;

		C2_ALLOC_PTR(ndom);
		if (ndom == NULL)
			return -ENOMEM;

		rc = c2_net_xprt_init(cs_xprts[xprt_idx]);
		if (rc == 0)
			rc = c2_net_domain_init(ndom, cs_xprts[xprt_idx]);
		else
			break;

		c2_list_link_init(&ndom->nd_col_linkage);
		c2_list_add_tail(&cs_colibri.c_ndoms, &ndom->nd_col_linkage);
	}

	return rc;
}

/**
   Finalises all the network domains within the global
   list in struct colibri.
   Once the c2_net_domain is finalised it frees the
   same.
 */
static void cs_net_domains_fini()
{
	struct c2_net_domain *ndom;
	struct c2_net_domain *ndom_next;
	int                   idx;

	c2_list_for_each_entry_safe(&cs_colibri.c_ndoms, ndom, ndom_next,
			struct c2_net_domain, nd_col_linkage) {

		c2_net_domain_fini(ndom);
		c2_list_del(&ndom->nd_col_linkage);
		c2_list_link_fini(&ndom->nd_col_linkage);
		c2_free(ndom);
	}

	for (idx = 0; idx < ARRAY_SIZE(cs_xprts); ++idx) {
		c2_net_xprt_fini(cs_xprts[idx]);
	}
}

/**
   Initialises a request handler context.
   A request handler context consists of the storage
   domain, database, cob domain, fol and request handler
   instance to be initialised.
   The request handler context is allocated and initialised
   per request handler in a colibri process per node.
   So, there can exists multiple request handlers and thus
   multiple request handler contexts in a single colibri process
   per node.

   @param stype Type of storage to be initialised
   @param stob_path Path at which storage is located
   @param dbpath Path at which database is located

   @see struct reqh_context

   @pre stype != NULL && stob_path != NULL && dbpath != NULL

   @retval 0 On success
	-errno On failure
 */
static int cs_reqh_ctx_init(const char *stype, const char *stob_path,
						const char *dbpath)
{
	int                      rc;
	struct reqh_context     *rctx;
	struct reqh_stobs       *rstob;
	struct c2_stob_domain   *sdom;

	C2_PRE(stype != NULL && stob_path != NULL && dbpath != NULL);

	C2_ALLOC_PTR(rctx);
	if (rctx == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rctx->rc_magic = REQH_CONTEXT_MAGIC;
	rc = cs_storage_init(stype, stob_path, dbpath, &rctx->rc_stob,
							&rctx->rc_db);
	if (rc != 0) {
		fputs("Storage initialisation failed", stderr);
		goto cleanup_rctx;
	}

	rctx->rc_cdom_id.id = ++cdom_id;
	rc = c2_cob_domain_init(&rctx->rc_cdom, &rctx->rc_db,
					&rctx->rc_cdom_id);
	if (rc != 0) {
		fputs("Cob domain initialisation failed", stderr);
		goto cleanup_rctx;
	}
	rc = c2_fol_init(&rctx->rc_fol, &rctx->rc_db);
	if (rc != 0) {
		fputs("Fol initialisation failed", stderr);
		goto cleanup_rctx;
	}
	rstob = &rctx->rc_stob;
	if (strcmp(rstob->stype, "AD") == 0 ||
		strcmp(rstob->stype, "ad") == 0)
		sdom = rstob->adstob;
	else
		sdom = rstob->linuxstob;

	rc = c2_reqh_init(&rctx->rc_reqh, NULL, sdom, &rctx->rc_db,
					&rctx->rc_cdom, &rctx->rc_fol);

	if (rc != 0) {
		fputs("Request handler initialisation failed", stderr);
		goto cleanup_rctx;
	}

	c2_list_link_init(&rctx->rc_reqh.rh_colibri_linkage);
	c2_list_add_tail(&cs_colibri.c_reqhs,
			&rctx->rc_reqh.rh_colibri_linkage);

	goto out;

cleanup_rctx:
	c2_free(rctx);
out:
	return rc;
}

/**
   Finalises a request handler context.
   Request handler corresponding to given context should be
   finalised before the context can be finalised.

   @param rctx Request handler context to be finalised

   @pre cs_reqh_ctx_invariant()
 */
static void cs_reqh_ctx_fini(struct reqh_context *rctx)
{
	C2_PRE(cs_reqh_ctx_invariant(rctx));

	c2_fol_fini(&rctx->rc_fol);
	c2_cob_domain_fini(&rctx->rc_cdom);
	cs_storage_fini(&rctx->rc_stob, &rctx->rc_db);
	c2_free(rctx);
}

/**
   Finalises all the request handler contexts within a
   colibri process on a node.
   Global list of c2_reqh in struct colibri is traversed,
   and every c2_reqh, it is first finalsed, and then its
   container struct reqh_context is finalised.
   Once reqh_context is finalised the memory allocated for
   the same is also released.
 */
static void cs_reqh_ctxs_fini()
{
	struct c2_reqh      *reqh;
	struct c2_reqh      *reqh_next;
	struct reqh_context *rctx;

	c2_list_for_each_entry_safe(&cs_colibri.c_reqhs, reqh, reqh_next,
			struct c2_reqh, rh_colibri_linkage) {
		cs_rpcmachines_fini(reqh);
		cs_services_fini(reqh);
		c2_list_del(&reqh->rh_colibri_linkage);
		c2_list_link_fini(&reqh->rh_colibri_linkage);
		c2_reqh_fini(reqh);
		rctx = container_of(reqh, struct reqh_context, rc_reqh);
		C2_ASSERT(cs_reqh_ctx_invariant(rctx));
		cs_reqh_ctx_fini(rctx);
	}
}

/**
   Initialises global colibri context, network domains
   for registered network transports and processors on a
   node.
 */
static int cs_colibri_init()
{
	int rc;

	c2_list_init(&cs_colibri.c_reqhs);
	c2_list_init(&cs_colibri.c_ndoms);

	rc = cs_net_domains_init();

	if (rc == 0) {
		rc = c2_processors_init();
		if (rc != 0) {
			cs_net_domains_fini();
			goto out;
		}
		/*
		   This is for test purpose, should be invoked from
		   corresponding module.
		 */
		c2_reqh_service_type_init(&mds_type, &mds_type_ops, "mds");
	}

out:
	return rc;
}

/**
   Finalises global colibri context, network domains and
   processors on a node.

   @see struct colibri
 */
static void cs_colibri_fini()
{
	/* For test purpose */
	c2_reqh_service_type_fini(&mds_type);

	cs_net_domains_fini();
	c2_list_fini(&cs_colibri.c_ndoms);
	c2_list_fini(&cs_colibri.c_reqhs);
	c2_processors_fini();
}

/**
   Signal handler registered so that pause()
   returns inorder to trigger proper cleanup.
 */
static void cs_term_handler(int signum)
{
	return;
}

/**
   Validates storage type

   @param stype Storage type

   @retval true If storage type exists
	false If storage type does not exists
 */
static bool validate_stype(const char *stype)
{
	C2_PRE(stype != NULL);

	return stype != NULL && ((strcmp(stype, "AD") == 0 ||
		strcmp(stype, "ad") == 0) ||
		(strcmp(stype, "Linux") == 0 ||
		strcmp(stype, "linux") == 0));
}

/**
   Looks up if given xprt is supported

   @param xprt_name Network transport name for lookup

   @retval 0 If transport found 
	-ENOENT If transport not found
 */
static int lookup_xprt(const char *xprt_name)
{
        int i;

	C2_PRE(xprt_name != NULL);

        for (i = 0; i < ARRAY_SIZE(cs_xprts); ++i)
                if (strcmp(xprt_name, cs_xprts[i]->nx_name) == 0)
                        return 0;
        return -ENOENT;
}

static void list_xprts(FILE *s)
{
        int i;

	C2_PRE(s != NULL);

        fprintf(s, "\nSupported transports:\n");
        for (i = 0; i < ARRAY_SIZE(cs_xprts); ++i)
                fprintf(s, "    %s\n", cs_xprts[i]->nx_name);
}

/**
   Checks if given network layer end point address
   is already in use.

   @param xprt Network transport
   @param ep   Network layer end point

   @retval 0 If endpoint is not in use
	-EADDRINUSE If endpoint is in use
 */
static int is_endpoint_inuse(char *xprt, char *nep)
{
	int                        rc;
	struct c2_net_domain      *ndom;
	struct c2_net_domain      *ndom_next;
	struct c2_net_transfer_mc *tm;
	struct c2_net_transfer_mc *tm_next;

	C2_PRE(xprt != NULL && nep != NULL);

	c2_list_for_each_entry_safe(&cs_colibri.c_ndoms, ndom, ndom_next,
			struct c2_net_domain, nd_col_linkage) {
		if (strcmp(xprt, ndom->nd_xprt->nx_name) == 0)
			break;
	}

	C2_ASSERT(ndom != NULL);

	rc = 0;
	c2_list_for_each_entry_safe(&ndom->nd_tms, tm, tm_next,
			struct c2_net_transfer_mc, ntm_dom_linkage) {

		if (strcmp(nep, tm->ntm_ep->nep_addr) == 0) {
			rc = -EADDRINUSE;
			break;
		}
	}

	return rc;
}

/**
   Validates end point address.
   End point address consists of 2 parts currently
   i.e. xport:network layer end point.

   @param ep Endpoint address to be validated

   @retval 0 On success
	-errno On failure
*/
static int validate_ep(char *ep)
{
	int   rc;
	char *epaddr;
	char *nep;
	char *xprt;

	C2_PRE(ep != NULL);

	/*
	  strtok() modifies the actual string so making a temporary
	  copy and using the same.
	 */
	C2_ALLOC_ARR(epaddr, strlen(ep) + 1);
	if (epaddr == NULL)
		return -ENOMEM;
	strcpy(epaddr, ep);
	xprt = strtok(epaddr, ":");
	rc = lookup_xprt(xprt);
	if (rc != 0)
		goto out;

	/* Network layer end point */
	nep = strtok(NULL, " ");
	if (nep == NULL) {
		rc = -EADDRNOTAVAIL;
		goto out;
	}
	
	rc = is_endpoint_inuse(xprt, nep);
	c2_free(epaddr);
out:
	return rc;
}

static void cs_usage(void)
{
	printf("\n usage:");
	printf("\n colibri_setup  [-i] [-h]");
	printf("\n                -r [-T storage type, default: linux]");
	printf("\n                -D Database -S storage name");
	printf("\n                -e xport:endpoint ...");
	printf("\n                -s service ...\n");
}

static void cs_info(void)
{
	printf("\n -h: prints usage");
	printf("\n -i: colibri_setup info");
	printf("\n");
	printf("\n -r: Starts request handler, this can be given multiple");
	printf("\n     times. Every request handler is a set of storage type,");
	printf("\n     database, storage, network endpoints and services,");
	printf("\n     thus every -r option should succeed with its other");
	printf("\n     set of options correspondingly");
	printf("\n     This option takes no arguments (optional: NO)");
	printf("\n");
	printf("\n -T: Specifies storage object domain type, options are");
	printf("\n     linux and AD");
	printf("\n     default: linux (optional: YES)");
	printf("\n");
	printf("\n -D: Database for given request handler ");
	printf("\n     e.g. ./c2db (optional: NO)");
	printf("\n");
	printf("\n -S: Path at which \"storage type\" storage is created,");
	printf("\n     If storage type is not specified it creates default");
	printf("\n     linux type storage object at given path");
	printf("\n     (optional: NO)");
	printf("\n");
	printf("\n -e: Endpoint address on which the client can connect to");
	printf("\n     inorder to send requests for processing");
	printf("\n     Every endpoint address comprises of 3 parts");
	printf("\n     Network transport:3-tuple network layer endpoint");
	printf("\n     A network layer endpoint contains a valid");
	printf("\n     ipaddress:port:ID");
	printf("\n     Currently supported network transports is bulk-sunrpc");
	printf("\n     e.g. bulk-sunrpc:127.0.0.1:1024:1");
	printf("\n     This can be specified multiple times in a request");
	printf("\n     handler set, although currently for same transport");
	printf("\n     the network layer end point address should have same");
	printf("\n     ipaddres:port:[id] 3-tuple (optional: NO)");
	printf("\n");
	printf("\n -s: Services to be started in given request handler");
	printf("\n     context. Once a service is started successfully, it");
	printf("\n     is registered with the request handler");
	printf("\n     (optional: NO)");
	printf("\n");
	printf("\n  e.g. ./colibri_setup -r -T AD -D c2db -S colibri_stob");
	printf("\n       -e bulk-sunrpc:127.0.0.1:1024:1 -s mds -r -T AD");
	printf("\n       -D c3db -S colibri_stob2 -e bulk-sunrpc:127.0.0.1:1024:2");
	printf("\n       -s mds\n");
}

/**
  Performs cleanup of entire colibri context.
 */
static void do_cleanup(void)
{
        cs_reqh_ctxs_fini();
        cs_colibri_fini();
        c2_fini();
}

int main(int argc, char **argv)
{
	int                     rc;
	int                     rh_index;
	const char             *stob_path = NULL;
	const char             *stype = NULL;
	const char             *dbpath = NULL;
	const char             *service = NULL;
	char                   *endpoint = NULL;
	struct sigaction        term_act;

	errno = 0;
	rc = c2_init();

	if (rc != 0) {
		fputs("\n Failed to initialise Colibri \n", stderr);
		errno = rc < 0 ? -rc:rc;
		goto out;
	}

	rc = cs_colibri_init();

	if (rc != 0) {
		c2_fini();
		errno = rc < 0 ? -rc:rc;
		goto out;
	}

	rh_index = 0;
        rc = C2_GETOPTS("colibri_setup", argc, argv,
		C2_VOIDARG('i', "Start request handler",
			LAMBDA(void, (void) 
			{
				cs_info();
				do_cleanup();
				exit(0);
			})),
		C2_VOIDARG('h', "Start request handler",
			LAMBDA(void, (void) 
			{
				cs_usage();
				do_cleanup();
				exit(0);
			})),
		C2_VOIDARG('r', "Start request handler",
			LAMBDA(void, (void)
			{
				C2_CNT_INC(rh_index);
			})),
		C2_STRINGARG('T', "Storage domain type",
			LAMBDA(void, (const char *str)
			{
				stype = str;
				if (stype == NULL)
					stype = "linux";
				else if (!validate_stype(stype))
					stype = "linux";
				printf("\n %s \n", stype);
			})),
                C2_STRINGARG('D', "Database environment path",
                        LAMBDA(void, (const char *str)
			{
				dbpath = str;
				if (dbpath == NULL) {
					fputs("COLIBRI: No db path",
						stderr);
					do_cleanup();
					exit(0);
				}
			})),
                C2_STRINGARG('S', "Storage name",
                        LAMBDA(void, (const char *str)
			{
				if (rh_index == 0 || dbpath == NULL) {
					fputs("COLIBRI: Missing arguments",
								stderr);
					do_cleanup();
					do_cleanup();
					exit(0);
				}
				stob_path = str;
				if (stob_path == NULL) {
					fputs("COLIBRI: Invalid storage path",
								stderr);
					do_cleanup();
					exit(0);
				}
				if (stype == NULL)
					stype = "linux";
				rc = cs_reqh_ctx_init(stype,
					stob_path, dbpath);
				if (rc != 0) {
					do_cleanup();
					errno = rc < 0 ? -rc:rc;
					exit(0);
				}
			})),
                C2_STRINGARG('e', "Network endpoint, eg:- transport:address",
			LAMBDA(void, (const char *str)
			{
				endpoint = (char *)str;
				if (rh_index == 0 || stob_path == NULL) {
					fputs("COLIBRI: Missing arguments",
								stderr);
					do_cleanup();
					exit(0);
				}
				rc = validate_ep(endpoint);
				if (rc == -EADDRINUSE) {
					fputs("COLIBRI: Duplicate end point",
								stderr);
					do_cleanup();
					errno = rc < 0 ? -rc:rc;
					exit(0);
					
				} else if (rc != 0) {
					fputs("COLIBRI: Invalid end point",
								stderr);
					list_xprts(stdout);
					do_cleanup();
					errno = rc < 0 ? -rc:rc;
					exit(0);
				}
				rc = cs_rpcmachine_init(endpoint, rh_index);
				if (rc != 0) {
					fputs("COLIBRI: Invalid end point",
								stderr);
					do_cleanup();
					errno = rc < 0 ? -rc:rc;
					exit(0);
				}
			})),
		C2_STRINGARG('s', "Start a service eg:- mds",
			LAMBDA(void, (const char *str)
			{
				service = str;
				if (rh_index == 0 || dbpath == NULL ||
					stob_path == NULL || endpoint == NULL) {
					fputs("COLIBRI: Missing arguments",
								stderr);
					do_cleanup();
					exit(0);
				}
				rc = cs_service_init(service, rh_index);
				if (rc != 0) {
					fputs("COLIBRI: Invalid service",
								stderr);
					do_cleanup();
					errno = rc < 0 ? -rc:rc;
					exit(0);
				}
			})));

	if (rc != 0 || rh_index == 0 || endpoint == NULL ||
		service == NULL || dbpath == NULL || stob_path == NULL) {
		fputs("COLIBRI: Please check the help or info and", stderr);
		fputs(" specify all the mandatory arguments", stderr);
		errno = rc < 0 ? -rc:rc;
		goto cleanup;
	}
	/*
	   Register a signal handler to capture SIGTERM signal
	   inorder to perform smooth cleanup.
	 */
	term_act.sa_handler = cs_term_handler;
	sigemptyset(&term_act.sa_mask);
	term_act.sa_flags = 0;
	sigaction(SIGTERM, &term_act, NULL);
	sigaction(SIGINT,  &term_act, NULL);
	sigaction(SIGQUIT, &term_act, NULL);
	sigaction(SIGABRT, &term_act, NULL);

	pause();

cleanup:
	do_cleanup();
out:
	return errno;
}

/** @} endgroup c2_setup */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
