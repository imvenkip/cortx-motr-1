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
 *                  Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 05/08/2011
 */
#pragma once

#ifndef __MERO_MERO_MERO_SETUP_H__
#define __MERO_MERO_MERO_SETUP_H__

#include <stdio.h> /* FILE */

#include "lib/tlist.h"
#include "reqh/reqh_service.h"
#include "stob/stob.h"
#include "net/buffer_pool.h"

/**
   @defgroup m0d Mero Setup

   Mero setup program configures a user space mero context
   on a node in a cluster.
   There exist a list of network transports supported by a node,
   which is used to initialise corresponding network domains per mero
   context, so there exist a network domain per network transport.
   There can exist multiple request handlers per mero context.
   Every mero context configures one or more request handler
   contexts, one per request handler, each containing a storage domain,
   data base, cob domain, fol and request handler to be initialised.
   Every request handler contains a list of rpc machines, each configured
   per given endpoint per network domain.
   Network domains are shared between multiple request handlers in a
   mero context.
   There exist multiple services within a mero context.
   Each service identifies a particular set of operations that can be
   executed on a particular node.
   Services are registered with the request handler which performs the
   execution of requests directed to a particular service. Thus the
   services run under request handler context.

   Mero setup can be done internally through mero code or externally
   through cli using m0d program. As mero setup configures
   the server it should be used in server side initialisation. if done
   through code, Following has to be done to configure a mero context:

   - Initialise mero context:
     For this you have to first define an array of network transports
     to be used in the mero context and pass it along with the array
     size to the initialisation routine.

   @note Also user should pass a output file descriptor to which the error
         messages will be directed.
   @code
   struct m0_mero mero_ctx;
   static struct m0_net_xprt *xprts[] = {
        &m0_net_lnet_xprt,
	...
    };

   m0_cs_init(&mero_ctx, xprts, ARRAY_SIZE(xprts), outfile);
   @endcode

   Define parameters for mero setup and setup environment as below,

   @code
   static char *cmd[] = { "m0d", "-r", "-T", "AD",
                   "-D", "cs_db", "-S", "cs_stob",
                   "-e", "lnet:172.18.50.40@o2ib1:12345:34:1",
                   "-s", "dummy"};

    m0_cs_setup_env(&mero_ctx, ARRAY_SIZE(cs_cmd), cs_cmd);
    @endcode

    Once the environment is setup successfully, the services can be started
    as below,
    @code
    m0_cs_start(&srv_mero_ctx);
    @endcode

    @note The specified services to be started should be registered before
          startup.

    Failure handling for m0d is done as follows,
    - As mentioned above, user must follow the sequence of m0_cs_init(),
      m0_cs_setup_env(), and m0_cs_start() in-order to setup m0_mero instance
      programmatically. If m0_cs_init() fails, user need not invoke m0_cs_fini(),
      although if m0_cs_init() succeeds and if further calls to m0d
      routines fail i.e m0_cs_setup_env() or cs_cs_start(), then user must invoke
      m0_cs_fini() corresponding to m0_cs_init().

    Similarly, to setup mero externally, using m0d program along
    with parameters specified as above.
    e.g. ./mero -r -T linux -D dbpath -S stobfile \
           -e xport:172.18.50.40@o2ib1:12345:34:1 -s service

    Below image gives an overview of entire mero context.
    @note This image is borrowed from the "New developer guide for mero"
          document in section "Starting Mero services".

    @image html "../../mero/DS-Reqh.gif"

   @{
 */

enum {
	M0_AD_STOB_ID_DEFAULT = 0x0,
	M0_AD_STOB_ID_LO      = 0xadf11e, /* AD file */
	M0_ADDB_STOB_ID_HI    = M0_AD_STOB_ID_DEFAULT,
	M0_ADDB_STOB_ID_LI    = 1,
};

enum {
	M0_LINUX_STOB = 0,
	M0_AD_STOB,
	M0_STOB_TYPE_NR,
};

/** String representations corresponding to the stob types. */
const char *m0_cs_stypes[M0_STOB_TYPE_NR];

/** Well-known stob ID for an addb stob. */
const struct m0_stob_id m0_addb_stob_id;

/**
 * Auxiliary structure used to pass command line arguments to cs_parse_args().
 */
struct cs_args {
	int   ca_argc;
	char *ca_argv[64];
};

/**
   Defines a mero context containing a set of network transports,
   network domains and request handler contexts.

   Every request handler context is a set of parsed values of setup arguments
   and corresponding in-memory representations of storage, database environment,
   cob domain, fol, network domains, services and request handler.
 */
struct m0_mero {
	/** Protects access to m0_mero members. */
	struct m0_rwlock          cc_rwlock;

	/**
	   Array of network transports supported in a mero context.
	 */
	struct m0_net_xprt      **cc_xprts;

	/**
	   Size of cc_xprts array.
	 */
	size_t                    cc_xprts_nr;

        /**
           List of network domain per mero context.

	   @see m0_net_domain::nd_app_linkage
         */
        struct m0_tl              cc_ndoms;

        /**
           List of request handler contexts running under one mero context
	   on a node.

	   @see cs_reqh_context::rc_linkage
         */
	struct m0_tl              cc_reqh_ctxs;

	/**
	   File to which the output is written.
	   This is set to stdout by default if no output file
	   is specified.
	   Default is set to stdout.
	   @see m0_cs_init()
	 */
	FILE                     *cc_outfile;
	/**
	 * List of buffer pools in mero context.
	 * @see cs_buffer_pool::cs_bp_linkage
	 */
	struct m0_tl             cc_buffer_pools;

	/**
	 * Minimum number of buffers in TM receive queue.
	 * @see m0_net_transfer_mc:ntm_recv_queue_length
	 * Default is set to M0_NET_TM_RECV_QUEUE_DEF_LEN.
	 */
	size_t                   cc_recv_queue_min_length;

	/** Maximum RPC message size. */
	size_t                   cc_max_rpc_msg_size;

	/** Segment size for any ADDB stob. */
	size_t                   cc_addb_stob_segment_size;

	/** command line arguments */
	struct cs_args		 cc_args;
};

/**
   Initialises mero context.

   @param cs_mero Represents a mero context
   @param xprts Array or network transports supported in a mero context
   @param xprts_nr Size of xprts array
   @param out File descriptor to which output is written
 */
int m0_cs_init(struct m0_mero *cs_mero,
	       struct m0_net_xprt **xprts, size_t xprts_nr, FILE *out);
/**
   Finalises mero context.
 */
void m0_cs_fini(struct m0_mero *cs_mero);

/**
   Configures mero context before starting the services.
   Parses the given arguments and allocates request handler contexts.
   Validates allocated request handler contexts which includes validation
   of given arguments and their values.
   Once all the arguments are validated, initialises network domains, creates
   and initialises request handler contexts, configures rpc machines each per
   request handler end point.

   @param cs_mero Mero context to be initialised
 */
int m0_cs_setup_env(struct m0_mero *cs_mero, int argc, char **argv);

/**
   Starts all the specified services in the mero context.
   Only once the mero environment is configured with network domains,
   request handlers and rpc machines, specified services are started.

   @param cs_mero Mero context in which services are started
 */
int m0_cs_start(struct m0_mero *cs_mero);

M0_INTERNAL struct m0_rpc_machine *m0_mero_to_rmach(struct m0_mero *mero);

M0_INTERNAL struct m0_stob_domain *m0_cs_stob_domain_find(struct m0_reqh *reqh,
					      const struct m0_stob_id *stob_id);

/**
   Find a request handler service within a given Mero instance.

   @param cctx Pointer to Mero context
   @param service_name Name of the service

   @pre cctx != NULL && service_name != NULL

   @retval  NULL of reqh instnace.
 */
struct m0_reqh *m0_cs_reqh_get(struct m0_mero *cctx, const char *service_name);

/**
 * Returns instance of struct m0_mero given a
 * request handler instance.
 * @pre reqh != NULL.
 */
M0_INTERNAL struct m0_mero *m0_cs_ctx_get(struct m0_reqh *reqh);

M0_INTERNAL struct m0_net_domain *m0_cs_net_domain_locate(struct m0_mero *cctx,
							  const char *xprtname);

/** @} endgroup m0d */

#endif /* __MERO_MERO_MERO_SETUP_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
