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

   Colibri setup program configures and starts a user space colibri
   environment on a node in a cluster.
   There exist a list of network transports supported by a node,
   which is used to initialise corresponding network domains per colibri
   environment, so there exist a network domain per network transport.
   There can exist multiple request handlers per colibri environment.
   Every colibri environment configures one or more request handler
   contexts, one per request handler, each containing a storage domain,
   data base, cob domain, fol and request handler to be initialised.
   Every request handler contains a list of rpc machines, each configured
   per given endpoint per network domain.
   Network domains are shared between multiple request handlers in a
   colibri environment.
   There exist multiple services within a colibri environment.
   Each service identifies a particular set of operations that can be
   executed on a particular node.
   Services are registered with the request handler which performs the
   execution of requests directed to a particular service. Thus the
   services run under request handler context.

   @{
 */

/**
   Defines a colibri environment containing a set of network transports,
   network domains and request handler contexts.

   Every request handler context is a set of parsed values of setup arguments
   and corresponding in memory represetations of storage, database environment,
   cob domain, fol, network domains, services and request handler.
 */
struct c2_colibri {
	/**
	   Array of netwrk transports supported in a colibri
	   environment.
	 */
	struct c2_net_xprt       **cc_xprts;
        /**
           List of network domain per colibri environment
           address space.
         */
        struct c2_list            cc_ndoms;

        /**
           List of request handler contexts running under
           one colibri environment address space on a node.
         */
	struct c2_list            cc_reqh_ctxs;
	/**
	   File to which the output is written.
	   This is set to stdout by default if no output file
	   is specified.
	   Default is set to stdout.

	   @see c2_cs_init()
	 */
	FILE                     *cc_outfile;

};

/**
   Initialises an instance of colibri environment.
   This includes initialising network resources like transports,
   and network domains.

   @param cs_colibri In memory representation of a colibri environment
   @param xprts Array or network transports supported by given
	colibri environment
   @param out File descriptor to which output is written

   @retval 0 On success
	-errno On failure
 */
int c2_cs_init(struct c2_colibri *cs_colibri, struct c2_net_xprt **xprts,
								FILE *out);
/**
   Finalises an instance of colibri environment.

   @param cs_colibri Instance of colibri environment to be finalised
 */
void c2_cs_fini(struct c2_colibri *cs_colibri);

/**
   Initialises the environment and starts the colibri environment with
   the specified arguments.

   @param cs_colibri Instance of colibri environment to be initialised

   @retval 0 On success
	-errno On failure
 */
int c2_cs_start(struct c2_colibri *cs_colibri, int argc, char **argv);

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

