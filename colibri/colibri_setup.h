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

#ifndef __COLIBRI_COLIBRI_COLIBRI_SETUP_H__
#define __COLIBRI_COLIBRI_COLIBRI_SETUP_H__

#include "lib/tlist.h"
#include "stob/stob.h"
#include "dtm/dtm.h"

/**
   @defgroup colibri_setup Configures user space colibri environment

   Colibri setup program configures a user space colibri context
   on a node in a cluster.
   There exist a list of network transports supported by a node,
   which is used to initialise corresponding network domains per colibri
   context, so there exist a network domain per network transport.
   There can exist multiple request handlers per colibri context.
   Every colibri context configures one or more request handler
   contexts, one per request handler, each containing a storage domain,
   data base, cob domain, fol and request handler to be initialised.
   Every request handler contains a list of rpc machines, each configured
   per given endpoint per network domain.
   Network domains are shared between multiple request handlers in a
   colibri context.
   There exist multiple services within a colibri context.
   Each service identifies a particular set of operations that can be
   executed on a particular node.
   Services are registered with the request handler which performs the
   execution of requests directed to a particular service. Thus the
   services run under request handler context.

   Colibri setup can be done internally through colibri code or externally
   through cli using colibri_setup program. As colibri setup configures
   the server it should be used in server side initialisation, if done
   through code.
   Following has to be done to configure a colibri context:

   - Initialise colibri context:
     For this you have to first define an array of network transports
     to be used in the colibri context and pass it along with the array
     size to the initialisation routine.

   @note Also user should pass a output file descriptor to which the error
         messages will be directed.
   @code
   struct c2_colibri colibri_ctx;
   static struct c2_net_xprt *xprts[] = {
        &c2_net_bulk_sunrpc_xprt,
	...
    };

   c2_cs_init(&colibri_ctx, xprts, ARRAY_SIZE(xprts), outfile);
   @endcode

   Define parameters for colibri setup and setup environment as below,

   @code
   static char *cmd[] = { "colibri_setup", "-r", "-T", "AD",
                   "-D", "cs_db", "-S", "cs_stob",
                   "-e", "bulk-sunrpc:127.0.0.1:1024:2",
                   "-s", "dummy"};

    c2_cs_setup_env(&colibri_ctx, ARRAY_SIZE(cs_cmd), cs_cmd);
    @endcode

    Once the environment is setup successfully, the services can be started
    as below,
    @code
    c2_cs_start(&srv_colibri_ctx);
    @endcode

    @note The specified services to be started should be registered before
          startup.

    Similarly, to setup colibri externally, using colibri_setup program along
    with parameters specified as above.
    e.g. ./colibri -r -T linux -D dbpath -S stobfile -e xport:127.0.0.1:1024:1
          -s service

    Below image gives an overview of entire colibri context.
    @note This image is borrowed from the "New developer guide for colibri"
          document in section "Starting Colibri services".

    @image html "../../colibri/DS-Reqh.gif"

   @{
 */

/**
   Defines a colibri context containing a set of network transports,
   network domains and request handler contexts.

   Every request handler context is a set of parsed values of setup arguments
   and corresponding in-memory representations of storage, database environment,
   cob domain, fol, network domains, services and request handler.
 */
struct c2_colibri {
	/**
	   Array of network transports supported in a colibri context.
	 */
	struct c2_net_xprt       **cc_xprts;

	/**
	   Size of cc_xprts array.
	 */
	int                        cc_xprts_nr;

        /**
           List of network domain per colibri context.

	   @see c2_net_domain::nd_app_linkage
         */
        struct c2_tl               cc_ndoms;

        /**
           List of request handler contexts running under one colibri context
	   on a node.

	   @see cs_reqh_context::rc_linkage
         */
	struct c2_tl               cc_reqh_ctxs;

	/**
	   File to which the output is written.
	   This is set to stdout by default if no output file
	   is specified.
	   Default is set to stdout.

	   @see c2_cs_init()
	 */
	FILE                      *cc_outfile;
};

/**
   Structure which encapsulates stob type and
   stob domain references for linux and ad stobs respectively.
 */
struct c2_cs_reqh_stobs {
	/**
	   Type of storage domain to be initialise (e.g. Linux or AD)
	 */
	const char            *rs_stype;
	/**
	   Linux storage domain type.
	 */
	struct c2_stob_domain *rs_ldom;
	/**
	   Allocation data storage domain type.
	 */
	struct c2_stob_domain *rs_adom;
	/**
           Front end storage object id, i.e. ad
         */
	struct c2_stob_id      rs_id_back;
	/**
           Front end storage object
         */
	struct c2_stob        *rs_stob_back;
	struct c2_dtx          rs_tx;
};

/**
   Initialises colibri context.

   @param cs_colibri Represents a colibri context
   @param xprts Array or network transports supported in a colibri context
   @param xprts_nr Size of xprts array
   @param out File descriptor to which output is written
 */
int c2_cs_init(struct c2_colibri *cs_colibri, struct c2_net_xprt **xprts,
						int xprts_nr, FILE *out);

/**
   Finalises colibri context.
 */
void c2_cs_fini(struct c2_colibri *cs_colibri);

/**
   Configures colibri context before starting the services.
   Parses the given arguments and allocates request handler contexts.
   Validates allocated request handler contexts which includes validation
   of given arguments and their values.
   Once all the arguments are validated, initialises network domains, creates
   and initialises request handler contexts, configures rpc machines each per
   request handler end point.

   @param cs_colibri Colibri context to be initialised
 */
int c2_cs_setup_env(struct c2_colibri *cs_colibri, int argc, char **argv);

/**
   Starts all the specified services in the colibri context.
   Only once the colibri environment is configured with network domains,
   request handlers and rpc machines, specified services are started.

   @param cs_colibri Colibri context in which services are started
 */
int c2_cs_start(struct c2_colibri *cs_colibri);

/**
   Returns server side rpc machine in a colibri context for given service
   and network transport.

   @retval Returns c2_rpcmachine if found, else returns NULL
 */
struct c2_rpcmachine *c2_cs_rpcmach_get(struct c2_colibri *cctx,
					const struct c2_net_xprt *xprt,
					const char *sname);

/**
   Returns server side transfer machine in a colibri context for given service
   and network transport.

   @retval Returns c2_net_transfer_mc if found,
	else returns NULL
 */
struct c2_net_transfer_mc *c2_cs_tm_get(struct c2_colibri *cctx,
					const struct c2_net_xprt *xprt,
					const char *service);

/**
   Initialises storage including database environment and stob domain of given
   type (e.g. linux or ad). There is a stob domain and a database environment
   created per request handler context.

   @param stob_type Type of stob to be initialised (e.g. linux or ad)
   @param stob_path Path at which storage object should be created
   @param stob Pre allocated struct reqh_stob_domain object encapsulates
               c2_stob_domain references for linux and ad stob types
   @param db Pre allocated struct c2_dbenv instance to be initialised

   @see struct reqh_stob_domain

   @pre stob_type != NULL && stob_path != NULL && stob != NULL && db != NULL

   @todo Use generic mechanism to generate stob ids
 */
int c2_cs_storage_init(const char *stob_type, const char *stob_path,
		       struct c2_cs_reqh_stobs *stob,
		       struct c2_dbenv *db);

/**
   Finalises storage for a request handler in a colibri context.

   @param stob Generic stob encapsulating c2_stob_domain references for linux
          and ad stobs to be finalised

   @see struct reqh_stob_domain

   @pre stob != NULL
 */
void c2_cs_storage_fini(struct c2_cs_reqh_stobs *stob);

/** @} endgroup colibri_setup */

/* __COLIBRI_COLIBRI_COLIBRI_SETUP_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

