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
 *                  Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 05/08/2011
 */
#pragma once

#ifndef __COLIBRI_COLIBRI_COLIBRI_SETUP_H__
#define __COLIBRI_COLIBRI_COLIBRI_SETUP_H__

#include <stdio.h> /* FILE */

#include "lib/tlist.h"
#include "reqh/reqh_service.h"
#include "stob/stob.h"
#include "net/buffer_pool.h"

/**
   @defgroup colibri_setup Colibri Setup

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
   the server it should be used in server side initialisation. if done
   through code, Following has to be done to configure a colibri context:

   - Initialise colibri context:
     For this you have to first define an array of network transports
     to be used in the colibri context and pass it along with the array
     size to the initialisation routine.

   @note Also user should pass a output file descriptor to which the error
         messages will be directed.
   @code
   struct c2_colibri colibri_ctx;
   static struct c2_net_xprt *xprts[] = {
        &c2_net_lnet_xprt,
	...
    };

   c2_cs_init(&colibri_ctx, xprts, ARRAY_SIZE(xprts), outfile);
   @endcode

   Define parameters for colibri setup and setup environment as below,

   @code
   static char *cmd[] = { "colibri_setup", "-r", "-T", "AD",
                   "-D", "cs_db", "-S", "cs_stob",
                   "-e", "lnet:172.18.50.40@o2ib1:12345:34:1",
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

    Failure handling for colibri_setup is done as follows,
    - As mentioned above, user must follow the sequence of c2_cs_init(),
      c2_cs_setup_env(), and c2_cs_start() in-order to setup c2_colibri instance
      programmatically. If c2_cs_init() fails, user need not invoke c2_cs_fini(),
      although if c2_cs_init() succeeds and if further calls to colibri_setup
      routines fail i.e c2_cs_setup_env() or cs_cs_start(), then user must invoke
      c2_cs_fini() corresponding to c2_cs_init().

    Similarly, to setup colibri externally, using colibri_setup program along
    with parameters specified as above.
    e.g. ./colibri -r -T linux -D dbpath -S stobfile \
           -e xport:172.18.50.40@o2ib1:12345:34:1 -s service

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
	/** Protects access to c2_colibri members. */
	struct c2_rwlock          cc_rwlock;

	/**
	   Array of network transports supported in a colibri context.
	 */
	struct c2_net_xprt      **cc_xprts;

	/**
	   Size of cc_xprts array.
	 */
	size_t                    cc_xprts_nr;

        /**
           List of network domain per colibri context.

	   @see c2_net_domain::nd_app_linkage
         */
        struct c2_tl              cc_ndoms;

        /**
           List of request handler contexts running under one colibri context
	   on a node.

	   @see cs_reqh_context::rc_linkage
         */
	struct c2_tl              cc_reqh_ctxs;

	/**
	   File to which the output is written.
	   This is set to stdout by default if no output file
	   is specified.
	   Default is set to stdout.
	   @see c2_cs_init()
	 */
	FILE                     *cc_outfile;
	/**
	 * List of buffer pools in colibri context.
	 * @see cs_buffer_pool::cs_bp_linkage
	 */
	struct c2_tl             cc_buffer_pools;

	/**
	 * Minimum number of buffers in TM receive queue.
	 * @see c2_net_transfer_mc:ntm_recv_queue_length
	 * Default is set to C2_NET_TM_RECV_QUEUE_DEF_LEN.
	 */
	size_t                   cc_recv_queue_min_length;

	/** Maximum RPC message size. */
	size_t                   cc_max_rpc_msg_size;

	struct c2_addb_ctx       cc_addb;
};

/**
   Initialises colibri context.

   @param cs_colibri Represents a colibri context
   @param xprts Array or network transports supported in a colibri context
   @param xprts_nr Size of xprts array
   @param out File descriptor to which output is written
 */
C2_INTERNAL int c2_cs_init(struct c2_colibri *cs_colibri,
			   struct c2_net_xprt **xprts, size_t xprts_nr,
			   FILE * out);

/**
   Finalises colibri context.
 */
C2_INTERNAL void c2_cs_fini(struct c2_colibri *cs_colibri);

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
C2_INTERNAL int c2_cs_setup_env(struct c2_colibri *cs_colibri, int argc,
				char **argv);

/**
   Starts all the specified services in the colibri context.
   Only once the colibri environment is configured with network domains,
   request handlers and rpc machines, specified services are started.

   @param cs_colibri Colibri context in which services are started
 */
C2_INTERNAL int c2_cs_start(struct c2_colibri *cs_colibri);

C2_INTERNAL struct c2_stob_domain *c2_cs_stob_domain_find(struct c2_reqh *reqh,
							  const struct
							  c2_stob_id *stob_id);

/**
   Find a request handler service within a given Colibri instance.

   @param cctx Pointer to Colibri context
   @param service_name Name of the service

   @pre cctx != NULL && service_name != NULL

   @retval  NULL of reqh instnace.
 */
struct c2_reqh *c2_cs_reqh_get(struct c2_colibri *cctx,
			       const char *service_name);

/**
 * Returns instance of struct c2_colibri given a
 * request handler instance.
 * @pre reqh != NULL.
 */
C2_INTERNAL struct c2_colibri *c2_cs_ctx_get(struct c2_reqh *reqh);

C2_INTERNAL struct c2_net_domain *c2_cs_net_domain_locate(struct c2_colibri
							  *cctx,
							  const char
							  *xprt_name);

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

