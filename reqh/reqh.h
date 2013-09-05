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
#include "layout/layout.h"
#include "ha/epoch.h"
#include "be/ut/helper.h"	/* XXX: prototype solution */

/**
   @defgroup reqh Request handler

   Request handler provides non-blocking infrastructure for fop execution.
   There typically is a single request handler instance per address space, once
   the request handler is initialised and ready to serve requests, it accepts
   a fop (file operation packet), interprets it by interacting with other sub
   systems and executes the desired file system operation.

   For every incoming fop, request handler creates a corresponding fom
   (file operation state machine), fop is executed in this fom context.
   For every fom, request handler performs some standard operations such as
   authentication, locating resources for its execution, authorisation of file
   operation by the user, &c. Once all the standard phases are completed, the
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
   Request handler states.

   See @ref MGMT-SVC-DLD-lspec-rh-sm "Request Handler State Machine".
   <!-- mgmt/svc/mgmt_svc.c -->
 */
enum m0_reqh_states {
	M0_REQH_ST_INIT = 0,
	M0_REQH_ST_MGMT_STARTED,
	M0_REQH_ST_NORMAL,
	M0_REQH_ST_DRAIN,
	M0_REQH_ST_SVCS_STOP,
	M0_REQH_ST_MGMT_STOP,
	M0_REQH_ST_STOPPED,

	M0_REQH_ST_NR
};

/**
   Request handler instance.
 */
struct m0_reqh {
	/** Request handler magic. */
	uint64_t                 rh_magic;

	/** State machine. */
	struct m0_sm             rh_sm;

	/**
	   State machine group.
	   Request handler services state machines (rs_sm) belong to this group.
	   The management service broadcasts on the group channel to notify
	   waiters of significant events.
	   @todo Replace rh_mutex and rh_sd_signal
	 */
	struct m0_sm_group       rh_sm_grp;

	struct m0_dtm		*rh_dtm;

	/** Database environment for this request handler. */
	struct m0_be_seg        *rh_dbenv;

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

	/** Pointer to the management service */
	struct m0_reqh_service  *rh_mgmt_svc;

        /**
	    RPC machines running in this request handler.
	    There is one rpc machine per request handler
	    end point.

	    @see m0_rpc_machine::rm_rh_linkage
	 */
        struct m0_tl             rh_rpc_machines;

	/** provides protected access to reqh members. */
	struct m0_rwlock         rh_rwlock;

	/**
	   Private, fully configured, ADDB machine for the request handler.
	   The first such machine created is used to configure the global
	   machine, ::m0_addb_gmc.
	 */
	struct m0_addb_mc        rh_addb_mc;

	struct m0_addb_ctx       rh_addb_ctx;

	/**
	    Channel to wait on for reqh shutdown or FOM termination.
	    @deprecated Replace with rh_sm_grp
	 */
	struct m0_chan           rh_sd_signal;
	struct m0_mutex          rh_mutex; /**< protect rh_sd_signal chan */

	/** Local service consuming reply. */
	struct m0_local_service *rh_svc;

	/**
	 * Layout domain for this request handler.
	 */
	struct m0_layout_domain  rh_ldom;

	/** HA domain which stores the epoch. */
	struct m0_ha_domain      rh_hadom;

	/**
	    Lockers to store private data

	    Since this variable has a zero length array, this should be
	    at the end of structure.
	    refer <http://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html>
	 */
	struct m0_reqh_lockers   rh_lockers;
};

/**
   reqh init args
  */
struct m0_reqh_init_args {
	struct m0_dtm           *rhia_dtm;
	/** Database environment for this request handler */
	struct m0_be_seg        *rhia_db;
	struct m0_mdstore       *rhia_mdstore;
	/** fol File operation log to record fop execution */
	struct m0_fol           *rhia_fol;
	struct m0_local_service *rhia_svc;
};

/**
   Initialises request handler instance provided by the caller.

   @param reqh Request handler instance to be initialised

   @todo use iostores instead of m0_cob_domain

   @see m0_reqh
   @post m0_reqh_invariant()
   @post m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_INIT
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
   Initialises db-dependant part of request handler.

   @pre dbenv != NULL
 */
M0_INTERNAL int m0_reqh_dbenv_init(struct m0_reqh *reqh,
				   struct m0_be_seg *dbenv);

/**
   Finalises db-dependant part of request handler.
 */
M0_INTERNAL void m0_reqh_dbenv_fini(struct m0_reqh *reqh);

M0_INTERNAL int m0_reqh_addb_mc_config(struct m0_reqh *reqh,
				       struct m0_stob *stob);

/**
   Get the state of the request handler.
 */
M0_INTERNAL int m0_reqh_state_get(struct m0_reqh *reqh);

/**
   Decide whether to process an incoming FOP.

   See @ref MGMT-SVC-DLD-lspec-rh-sm "Request Handler State Machine".
   <!-- mgmt/svc/mgmt_svc.c -->

   @pre m0_rewlock_read_lock(&reqh->rh_rwlock)
 */
M0_INTERNAL int m0_reqh_fop_allow(struct m0_reqh *reqh, struct m0_fop *fop);

/**
   Submit fop for request handler processing.
   Request handler initialises fom corresponding to this fop, finds appropriate
   locality to execute this fom, and enqueues the fom into its runq.
   Fop processing results are reported by other means (ADDB, reply fops, error
   messages, etc.) so this function returns nothing.

   @param reqh, request handler processing the fop
   @param fop, fop to be executed

   @pre reqh != null
   @pre fop != null
 */
M0_INTERNAL int m0_reqh_fop_handle(struct m0_reqh *reqh, struct m0_fop *fop);

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
   Start the management service.

   @pre m0_reqh_state_get(reqh) == M0_REQH_ST_INIT
   @pre reqh->rh_mgmt_svc == NULL
   @post m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED ||
         m0_reqh_state_get(reqh) == M0_REQH_ST_INIT
   @post ergo(m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED,
              reqh->rh_mgmt_svc != NULL)
   @post ergo(m0_reqh_state_get(reqh) == M0_REQH_ST_INIT,
              reqh->rh_mgmt_svc == NULL)
 */
M0_INTERNAL int m0_reqh_mgmt_service_start(struct m0_reqh *reqh);

/**
   Notify the request handler that normal operation should commence.

   The subroutine does not enforce that a management service is
   configured because UTs frequently do not require this.

   @pre m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED ||
        m0_reqh_state_get(reqh) == M0_REQH_ST_INIT
   @post m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL
 */
M0_INTERNAL void m0_reqh_start(struct m0_reqh *reqh);

/**
   Returns a count of services in a specified state.
   @param reqh request handler
   @param state Service state
   @pre M0_IN(m0_reqh_state_get(reqh),
              (M0_REQH_ST_MGMT_STARTED, M0_REQH_ST_NORMAL,
	       M0_REQH_ST_DRAIN, M0_REQH_ST_SVCS_STOP)
 */
M0_INTERNAL int m0_reqh_services_state_count(struct m0_reqh *reqh, int state);

/**
   Initiates the termination of services.

   @param reqh request handler to be shutdown
   @pre m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL
   @post m0_reqh_state_get(reqh) == M0_REQH_ST_DRAIN
   @see m0_reqh_service_prepare_to_stop(), m0_reqh_fom_domain_idle_wait()
 */
M0_INTERNAL void m0_reqh_shutdown(struct m0_reqh *reqh);

/**
   Initiates the termination of services and then wait for FOMs to
   terminate.

   @param reqh request handler to be shutdown
   @pre m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL
   @post m0_reqh_state_get(reqh) == M0_REQH_ST_DRAIN
   @see m0_reqh_service_prepare_to_stop(), m0_reqh_fom_domain_idle_wait()
 */
M0_INTERNAL void m0_reqh_shutdown_wait(struct m0_reqh *reqh);

/**
   Stops and finalises all the services registered with a request handler,
   but not the management service.

   @pre m0_reqh_state_get(reqh) == M0_REQH_ST_DRAIN ||
        m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STARTED ||
        m0_reqh_state_get(reqh) == M0_REQH_ST_INIT
   @post m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STOP ||
         m0_reqh_state_get(reqh) == M0_REQH_ST_STOPPED
   @post ergo(reqh->rh_mgmt_svc != NULL,
              m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STOP)
   @post ergo(reqh->rh_mgmt_svc == NULL,
              m0_reqh_state_get(reqh) == M0_REQH_ST_STOPPED)
   @see m0_reqh_service_stop()
   @see m0_reqh_service_fini()
   @see m0_reqh_mgmt_service_stop()
 */
M0_INTERNAL void m0_reqh_services_terminate(struct m0_reqh *reqh);

/**
   Stop the management service.

   @pre m0_reqh_state_get(reqh) == M0_REQH_ST_MGMT_STOP
   @post m0_reqh_state_get(reqh) == M0_REQH_ST_STOPPED
   @post reqh->rh_mgmt_svc == NULL
 */
M0_INTERNAL void m0_reqh_mgmt_service_stop(struct m0_reqh *reqh);

/**
    Initialises global reqh objects like reqh fops and addb context,
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
