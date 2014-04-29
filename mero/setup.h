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
#include "lib/types.h"        /* m0_uint128 */
#include "reqh/reqh_service.h"
#include "stob/stob.h"
#include "db/db.h"
#include "net/lnet/lnet.h"    /* M0_NET_LNET_XEP_ADDR_LEN */
#include "net/buffer_pool.h"
#include "mdstore/mdstore.h"  /* m0_mdstore */
#include "fol/fol.h"          /* m0_fol */
#include "reqh/reqh.h"        /* m0_reqh */
#include "yaml.h"             /* yaml_document_t */

#include "be/ut/helper.h"	/* XXX: prototype solution */

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
   static char *cmd[] = { "m0d", "-T", "AD",
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
      programmatically. If m0_cs_init() fails, user need not invoke
      m0_cs_fini(), although if m0_cs_init() succeeds and if further calls to
      m0d routines fail i.e m0_cs_setup_env() or cs_cs_start(), then user must
      invoke m0_cs_fini() corresponding to m0_cs_init().

    Similarly, to setup mero externally, using m0d program along
    with parameters specified as above.
    e.g. ./m0d -T linux -D dbpath -S stobfile \
           -e xport:172.18.50.40@o2ib1:12345:34:1 -s service

    Below image gives an overview of entire mero context.
    @note This image is borrowed from the "New developer guide for mero"
	  document in section "Starting Mero services".

    @image html "../../mero/DS-Reqh.gif"

   @{
 */

enum {
	M0_SETUP_DEFAULT_POOL_WIDTH = 10
};

enum {
	M0_AD_STOB_KEY_DEFAULT   = 0x0,
	M0_AD_STOB_LINUX_DOM_KEY = 0xadf11e, /* AD file */
	M0_ADDB_STOB_KEY         = 1,
};

enum stob_type {
	M0_LINUX_STOB,
	M0_AD_STOB,
	M0_STOB_TYPE_NR
};

/** String representations corresponding to the stob types. */
M0_EXTERN const char *m0_cs_stypes[M0_STOB_TYPE_NR];

/** Well-known stob ID for an addb stob. */
M0_EXTERN const uint64_t m0_addb_stob_key;

/**
 * Auxiliary structure used to pass command line arguments to cs_parse_args().
 */
struct cs_args {
	int   ca_argc;
	char *ca_argv[64];
};

/**
   Contains extracted network endpoint and transport from mero endpoint.
 */
struct cs_endpoint_and_xprt {
	/**
	   mero endpoint specified as argument.
	   Used for ADDB purpose.
	 */
	const char      *ex_cep;
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
	uint64_t         ex_magix;
	/** Linkage into reqh context endpoint list, m0_reqh_context::rc_eps */
	struct m0_tlink  ex_linkage;
	/**
	   Unique Colour to be assigned to each TM.
	   @see m0_net_transfer_mc::ntm_pool_colour.
	 */
	uint32_t	 ex_tm_colour;
};

/**
 * Represent devices configuration file in form of yaml document.
 * @note This is temporary implementation in-order to configure device as
 *       a stob. This may change when confc implementation lands into master.
 * @todo XXX FIXME: confc has landed ages ago.
 */
struct cs_stob_file {
	bool            sf_is_initialised;
	yaml_document_t sf_document;
};

/**
   Structure which encapsulates stob type and
   stob domain references for linux and ad stobs respectively.
 */
struct cs_stobs {
	/** Type of storage domain to be initialise (e.g. Linux or AD) */
	enum stob_type         s_stype;
	/** Linux storage domain. */
	struct m0_stob_domain *s_sdom;
	/** Devices configuration. */
	struct cs_stob_file    s_sfile;
	/** List of AD stobs */
	struct m0_tl           s_adstobs;
};

/**
   Tracks ADDB stob per reqh
   cas_stob is the hard-coded stob, that is created during
   @see cs_addb_storage_init(), on which actual ADDB records go.
 */
struct cs_addb_stob {
	/** ADDB Storage domain for a request handler ADDB machine */
	struct cs_stobs cas_stobs;
	struct m0_stob *cas_stob;
};

/** States of m0_mero::cc_reqh_ctx. */
enum cs_reqh_ctx_states {
	RC_UNINITIALISED,
	RC_INITIALISED
};

/**
   Represents a request handler environment.
   It contains configuration information about the various global entities
   to be configured and their corresponding instances that are needed to be
   initialised before the request handler is started, which by itself is
   contained in the same structure.
 */
struct m0_reqh_context {
	/** Storage path for request handler context. */
	const char                  *rc_stpath;

	/** ADDB Storage location for request handler ADDB machine */
	const char                  *rc_addb_stlocation;

	/** Path to device configuration file. */
	const char                  *rc_dfilepath;

	/** Type of storage to be initialised. */
	const char                  *rc_stype;

	/** Database environment path for request handler context. */
	const char                  *rc_dbpath;

	/** Services running in request handler context. */
	char                       **rc_services;

	/** Service UUIDs */
	struct m0_uint128           *rc_service_uuids;

	/** Number of services configured in request handler context. */
	uint32_t                     rc_nr_services;

	/** Maximum number of services allowed per request handler context. */
	int                          rc_max_services;

	/** Endpoints and xprts per request handler context. */
	struct m0_tl                 rc_eps;

	/**
	    State of a request handler context, i.e. RC_INITIALISED or
	    RC_UNINTIALISED.
	 */
	enum cs_reqh_ctx_states      rc_state;

	/** Storage domain for a request handler */
	struct cs_stobs              rc_stob;

	/** ADDB specific stob information */
	struct cs_addb_stob          rc_addb_stob;

	/** Database used by the request handler */
	struct m0_dbenv              rc_db;
	struct m0_be_seg            *rc_beseg;

	/** Path to the configuration database to be used by confd service. */
	const char                  *rc_confdb;

	/** Cob domain to be used by the request handler */
	struct m0_mdstore            rc_mdstore;

	struct m0_cob_domain_id      rc_cdom_id;

	/** File operation log for a request handler */
	struct m0_fol               *rc_fol;

	/** Request handler instance to be initialised */
	struct m0_reqh               rc_reqh;

	/** Reqh context magic */
	uint64_t                     rc_magix;

	/** Backlink to struct m0_mero. */
	struct m0_mero              *rc_mero;

	/**
	 * Minimum number of buffers in TM receive queue.
	 * Default is set to m0_mero::cc_recv_queue_min_length
	 */
	uint32_t                     rc_recv_queue_min_length;

	/**
	 * Maximum RPC message size.
	 * Default value is set to m0_mero::cc_max_rpc_msg_size
	 * If value of cc_max_rpc_msg_size is zero then value from
	 * m0_net_domain_get_max_buffer_size() is used.
	 */
	uint32_t                     rc_max_rpc_msg_size;
};

/**
   Defines "Mero context" structure, which contains information on
   network transports, network domains and a request handler.
 */
struct m0_mero {
	/** Protects access to m0_mero members. */
	struct m0_rwlock            cc_rwlock;

	struct m0_reqh_context      cc_reqh_ctx;

	/** Array of network transports supported in a mero context. */
	struct m0_net_xprt        **cc_xprts;

	/** Size of cc_xprts array. */
	size_t                      cc_xprts_nr;

	/**
	   List of network domain per mero context.

	   @see m0_net_domain::nd_app_linkage
	 */
	struct m0_tl                cc_ndoms;

	/**
	   File to which the output is written.
	   This is set to stdout by default if no output file
	   is specified.
	   Default is set to stdout.
	   @see m0_cs_init()
	 */
	FILE                       *cc_outfile;

	/**
	 * List of buffer pools in mero context.
	 * @see cs_buffer_pool::cs_bp_linkage
	 */
	struct m0_tl                cc_buffer_pools;

	/**
	 * Minimum number of buffers in TM receive queue.
	 * @see m0_net_transfer_mc:ntm_recv_queue_length
	 * Default is set to M0_NET_TM_RECV_QUEUE_DEF_LEN.
	 */
	size_t                      cc_recv_queue_min_length;

	size_t                      cc_max_rpc_msg_size;

	/** Segment size for any ADDB stob. */
	size_t                      cc_addb_stob_segment_size;

	/** "stats" service endpoint. */
	struct cs_endpoint_and_xprt cc_stats_svc_epx;

	/** List of ioservice end points. */
	struct m0_tl                cc_ios_eps;

	/** List of mdservice end points. */
	struct m0_tl                cc_mds_eps;

	uint32_t                    cc_pool_width;

	char                       *cc_profile;
	char                       *cc_confd_addr;

	/** Run as a daemon? */
	bool                        cc_daemon;

	/** Run from mkfs? */
	bool                        cc_mkfs;

        /** Force to override found filesystem during mkfs. */
	bool                        cc_force;

	/** Command line arguments. */
	struct cs_args		    cc_args;
};

enum {
	CS_MAX_EP_ADDR_LEN = 86 /* "lnet:" + M0_NET_LNET_XEP_ADDR_LEN */
};
M0_BASSERT(CS_MAX_EP_ADDR_LEN >= sizeof "lnet:" + M0_NET_LNET_XEP_ADDR_LEN);

struct cs_ad_stob {
	/** Allocation data storage domain.*/
	struct m0_stob_domain *as_dom;
	/** Back end storage object. */
	struct m0_stob        *as_stob_back;
	uint64_t               as_magix;
	struct m0_tlink        as_linkage;
};

/**
   Initialises mero context.

   @param cs_mero Represents a mero context
   @param xprts Array or network transports supported in a mero context
   @param xprts_nr Size of xprts array
   @param out File descriptor to which output is written
   @param should the storage be prepared just like mkfs does?
 */
int m0_cs_init(struct m0_mero *cs_mero,
	       struct m0_net_xprt **xprts, size_t xprts_nr, FILE *out, bool mkfs);
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

/**
 * Accesses the request handler.
 *
 * @note Returned pointer is never NULL.
 */
struct m0_reqh *m0_cs_reqh_get(struct m0_mero *cctx);

/**
 * Returns instance of struct m0_mero given a
 * request handler instance.
 * @pre reqh != NULL.
 */
M0_INTERNAL struct m0_mero *m0_cs_ctx_get(struct m0_reqh *reqh);

/**
 * Finds network domain for specified network transport in a given mero
 * context.
 *
 * @pre cctx != NULL && xprt_name != NULL
 */
M0_INTERNAL struct m0_net_domain *m0_cs_net_domain_locate(struct m0_mero *cctx,
							  const char *xprtname);

/**
 * Extract network layer endpoint and network transport from end point string.
 *
 * @pre ep != NULL
 */
M0_INTERNAL int m0_ep_and_xprt_extract(struct cs_endpoint_and_xprt *epx,
				       const char *ep);

M0_TL_DESCR_DECLARE(cs_eps, extern);
M0_TL_DECLARE(cs_eps, M0_INTERNAL, struct cs_endpoint_and_xprt);
M0_BOB_DECLARE(M0_INTERNAL, cs_endpoint_and_xprt);

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
