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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *			Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

#pragma once

#ifndef __MERO_REQH_REQH_H__
#define __MERO_REQH_REQH_H__

#include "lib/tlist.h"
#include "lib/lockers.h"
#include "lib/bob.h"

#include "sm/sm.h"
#include "fop/fom.h"

/**
   @defgroup reqh Request handler

   Request handler provides non-blocking infrastructure for fop execution.
   There typically is a single request handler instance per address space, once
   the request handler is initialised and ready to serve requests, it accepts
   a fop (file operation packet), iterpretes it by interacting with other sub
   systems and executes the desired file system operation.

   For every incoming fop, request handler creates a corresponding fom
   (file operation state machine), fop is executed in this fom context.
   For every fom, request handler performs some standard operations such as
   authentication, locating resources for its execution, authorisation of file
   operation by the user, &tc. Once all the standard phases are completed, the
   fop specific operation is executed.

   @see <a href="https://docs.google.com/a/xyratex.com
/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjA2Zmc0N3I3Z2Y&hl=en_US">
High level design of M0 request handler</a>
   @{
 */

struct m0_fol;
struct m0_fop;
struct m0_net_xprt;
struct m0_rpc_machine;
struct m0_local_service_ops;

M0_LOCKERS_DECLARE(M0_EXTERN, m0_reqh, 32);

/** Local reply consumer service (testing or replicator) */
struct m0_local_service {
	const struct m0_local_service_ops    *s_ops;
};

struct m0_local_service_ops {
	void (*lso_fini) (struct m0_local_service *service, struct m0_fom *fom);
};

/**
   Request handler instance.
 */
struct m0_reqh {
	struct m0_dtm		*rh_dtm;

	/** Database environment for this request handler. */
	struct m0_dbenv         *rh_dbenv;

	/** Mdstore for this request handler. */
	struct m0_mdstore       *rh_mdstore;

	/** Fol pointer for this request handler. */
	struct m0_fol		*rh_fol;

	/** Fom domain for this request handler. */
	struct m0_fom_domain	 rh_fom_dom;

        /**
	    Services registered with this request handler.

	    @see m0_reqh_service::rs_linkage
	 */
        struct m0_tl             rh_services;

        /**
	    RPC machines running in this request handler
	    There is one rpc machine per request handler
	    end point.

	    @see m0_rpc_machine::rm_rh_linkage
	 */
        struct m0_tl             rh_rpc_machines;

	/** provides protected access to reqh members. */
	struct m0_rwlock         rh_rwlock;

	/**
	    True if request handler received a shutdown signal.
	    Request handler should not process any further requests
	    if this flag is set.
	 */
	bool                     rh_shutdown;

	/**
	   Private, fully configured, ADDB machine for the request handler.
	   The first such machine created is used to configure the global
	   machine, ::m0_addb_gmc.
	 */
	struct m0_addb_mc        rh_addb_mc;

	/** @deprecated */
	struct m0_addb_ctx       rh_addb;

	struct m0_addb_ctx       rh_addb_ctx;

	/**
	    Channel to wait on for reqh shutdown.
	 */
	struct m0_chan           rh_sd_signal;

	/**
	    Lockers to store private data
	 */
	struct m0_reqh_lockers   rh_lockers;

	/** Request handler magic. */
	uint64_t                 rh_magic;

	/** Local service consuming reply. */
	struct m0_local_service *rh_svc;
};

/**
   reqh init args
  */
struct m0_reqh_init_args {
	struct m0_dtm           *rhia_dtm;
	/** Database environment for this request handler */
	struct m0_dbenv         *rhia_db;
	struct m0_mdstore       *rhia_mdstore;
	/** fol File operation log to record fop execution */
	struct m0_fol           *rhia_fol;
	struct m0_local_service *rhia_svc;
	/** Hard-coded stob to store ADDB records
	    @see cs_addb_storage_init()
	  */
	struct m0_stob          *rhia_addb_stob;
};

/**
   Initialises request handler instance provided by the caller.

   @param reqh Request handler instance to be initialised

   @todo use iostores instead of m0_cob_domain

   @see m0_reqh
   @post m0_reqh_invariant()
 */
M0_INTERNAL int m0_reqh_init(struct m0_reqh *reqh,
			     const struct m0_reqh_init_args *args);

M0_INTERNAL bool m0_reqh_invariant(const struct m0_reqh *reqh);

/**
  */
#define M0_REQH_INIT(reqh, ...)                                 \
	m0_reqh_init((reqh), &(const struct m0_reqh_init_args) { \
		     __VA_ARGS__ })

/**
   Destructor for request handler, no fop will be further executed
   in the address space belonging to this request handler.

   @param reqh, request handler to be finalised

   @pre reqh != NULL
 */
M0_INTERNAL void m0_reqh_fini(struct m0_reqh *reqh);

/**
   Submit fop for request handler processing.
   Request handler intialises fom corresponding to this fop, finds appropriate
   locality to execute this fom, and enqueues the fom into its runq.
   Fop processing results are reported by other means (ADDB, reply fops, error
   messages, etc.) so this function returns nothing.

   @param reqh, request handler processing the fop
   @param fop, fop to be executed

   @pre reqh != null
   @pre fop != null
 */
M0_INTERNAL void m0_reqh_fop_handle(struct m0_reqh *reqh, struct m0_fop *fop);

/**
   Subroutine to generate ADDB statistics on resources consumed and managed
   by a resource handler.
   The subroutine is intended to be called from a FOM within the resource
   handler.
 */
M0_INTERNAL void m0_reqh_stats_post_addb(struct m0_reqh *reqh);

/**
   Waits on the request handler channel (m0_reqh::rh_sd_signal) until the
   request handler FOM domain (m0_reqh::rh_fom_dom) is idle.

   @note Use with caution. This can block forever if FOMs do not terminate.
   @see m0_fom_domain_is_idle()
   @see m0_reqh_shutdown_wait()
 */
M0_INTERNAL void m0_reqh_fom_domain_idle_wait(struct m0_reqh *reqh);

/**
   Initiates the termination of services and then wait for FOMs to
   terminate.

   @param reqh request handler to be shutdown
   @see m0_reqh_service_prepare_to_stop(), m0_reqh_fom_domain_idle_wait()
 */
M0_INTERNAL void m0_reqh_shutdown_wait(struct m0_reqh *reqh);

/**
   Stops and finalises all the services registered with a request handler.
   @see m0_reqh_service_stop()
   @see m0_reqh_service_fini()
 */
M0_INTERNAL void m0_reqh_services_terminate(struct m0_reqh *reqh);

/**
    Initializes global reqh objects like reqh fops and addb context,
    invoked from m0_init().
 */
M0_INTERNAL int m0_reqhs_init(void);

/**
   Finalises global reqh objects, invoked from m0_fini().
*/
M0_INTERNAL void m0_reqhs_fini(void);

/** Returns number of localities in request handler FOM domain. */
M0_INTERNAL uint64_t m0_reqh_nr_localities(const struct m0_reqh *reqh);

/** Descriptor for tlist of request handler services. */
M0_TL_DESCR_DECLARE(m0_reqh_svc, M0_EXTERN);
M0_TL_DECLARE(m0_reqh_svc, M0_INTERNAL, struct m0_reqh_service);
M0_BOB_DECLARE(M0_EXTERN, m0_reqh_service);

/** Descriptor for tlist of rpc machines. */
M0_TL_DESCR_DECLARE(m0_reqh_rpc_mach, extern);
M0_TL_DECLARE(m0_reqh_rpc_mach, , struct m0_rpc_machine);

/** @} endgroup reqh */

/* __MERO_REQH_REQH_H__ */
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
