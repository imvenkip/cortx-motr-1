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
	   Array of network transports supported in a colibri
	   context.
	 */
	struct c2_net_xprt       **cc_xprts;

	/**
	   Size of cc_xprts array.
	 */
	int                       cc_xprts_nr;
        /**
           List of network domain per colibri context.
         */
        struct c2_list            cc_ndoms;

        /**
           List of request handler contexts running under
           one colibri context on a node.
         */
	struct c2_list            cc_reqh_ctxs;
	/**
	   File to which the output is written.
	   This is set to stdout by default if no output file
	   is specified.
	   Default is set to stdout.

	   @see c2_cs_init()
	 */
	FILE               *cc_outfile;

};

/**
   Initialises colibri context.

   @param cs_colibri Represents a colibri context
   @param xprts Array or network transports supported in a colibri
		context
   @param xprts_nr Size of xprts array
   @param out File descriptor to which output is written

   @retval 0 On success
	-errno On failure
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
   Validates allocated request handler contexts which includes
   validation of given arguments and their values.
   Once all the arguments are validated, initialises network domains,
   creates and initialises request handler contexts, configures rpc
   machines each per request handler end point.

   @param cs_colibri Colibri context to be initialised

   @retval 0 On success
	-errno on failure
 */
int c2_cs_setup_env(struct c2_colibri *cs_colibri, int argc, char **argv);

/**
   Starts all the specified services in the colibri context.
   Only once the colibri environment is configured with network domains,
   request handlers and rpc machines, specified services are started.

   @param cs_colibri Colibri context in which services are started

   @retval 0 If all the services are started successfuly
	-errno If services fail to startup
 */
int c2_cs_start(struct c2_colibri *cs_colibri);

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

