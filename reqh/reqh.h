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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>,
 *			Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/19/2010
 */

#pragma once

#ifndef __MERO_REQH_REQH_H__
#define __MERO_REQH_REQH_H__

#include "lib/tlist.h"
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

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkNXg4cXpfMjA2Zmc0N3I3Z2Y&hl=en_US
   @{
 */

struct m0_fol;
struct m0_fop;
struct m0_net_xprt;
struct m0_rpc_machine;

enum {
        REQH_KEY_MAX = 32
};

struct m0_local_service_ops;

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

	struct m0_addb_ctx       rh_addb;

	/**
	    Channel to wait on for reqh shutdown.
	 */
	struct m0_chan           rh_sd_signal;

	/**
	 * Request handler key data.
	 *
	 * @see m0_reqh_key_init()
	 */
        void                    *rh_key[REQH_KEY_MAX];
	/** Request handler magic. */
	uint64_t                 rh_magic;

	/** Local service consuming reply. */
	struct m0_local_service *rh_svc;
};

/**
   Initialises request handler instance provided by the caller.

   @param reqh Request handler instance to be initialised
   @param db Database environment for this request handler
   @param cdom Cob domain for this request handler
   @param fol File operation log to record fop execution

   @todo use iostores instead of m0_cob_domain

   @see m0_reqh
   @post m0_reqh_invariant()
 */
M0_INTERNAL int m0_reqh_init(struct m0_reqh *reqh, struct m0_dtm *dtm,
			     struct m0_dbenv *db, struct m0_mdstore *mdstore,
			     struct m0_fol *fol, struct m0_local_service *svc);

M0_INTERNAL bool m0_reqh_invariant(const struct m0_reqh *reqh);

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
   Waits on m0_reqh::rh_sd_signal using the given clink until
   m0_fom_domain_is_idle().

   @param reqh request handler to be shutdown
 */
M0_INTERNAL void m0_reqh_shutdown_wait(struct m0_reqh *reqh);

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

/**
   @name reqhkey

   This infrastructure allows request handler to be a central repository of
   data, typically used by the request handler services.
   This not only allows services to efficiently share the data with other
   services running in the same request handler but also avoids duplicate
   implementation of similar kind of framework.

   Following interfaces are of interest to any request handler service intending
   to store and share its specific data with the corresponding request handler,
   - m0_reqh_key_init()
     Returns a new request handler key to access the stored data.
     Same key should be used in-order to share the data among multiple request
     handler entities if necessary.
     @note Key cannot exceed beyond REQH_KEY_MAX range for the given request
           handler.
     @see m0_reqh::rh_key
     @see ::keymax

   - m0_reqh_key_find()
     Locates and returns the data corresponding to the key in the request handler.
     The key is used to locate the data in m0_reqh::rh_key[]. If the data
     is NULL, then size amount of memory is allocated for the data and returned.
     @note As request handler itself does not have any knowledge about the
     purpose and usage of the allocated data, it is the responsibility of the
     caller to initialise the allocated data and verify the consistency of the
     same throughout its existence.

   - m0_reqh_key_fini()
     Destroys the request handler resource accessed by the given key.
     @note This simply destroys the allocated data without formally looking
     into its contents. Thus the caller must properly finalise the data contents
     before invoking this function. Also if the data was shared between multiple
     services, the caller must make sure that there are no more reference on the
     same.

     Below pseudo code illustrates the usage of reqhkey interfaces mentioned
     above,

     @code

     struct foo {
	...
	bool is_initialised;
     };
     static bool     foo_key_is_initialised;
     static uint32_t foo_key;
     int bar_init()
     {
	struct foo *data;

	if (!foo_key_is_initialised)
		foo_key = m0_reqh_key_init(); //get new reqh data key

	data = m0_reqh_key_find(reqh, foo_key, sizeof *foo);
	if (!data->foo_is_initialised)
		foo_init(data);
	...
     }

     void bar_fini()
     {
	struct foo *data;

	data = m0_reqh_key_find(reqh, foo_key, sizeof *foo);
	foo_fini(data);
	m0_reqh_key_fini(reqh, foo_key);
     }
     @endcode

     For more details please refer to @ref m0_cobfid_map_get() and
     @ref m0_cobfid_map_put() interfaces in ioservice/cobfid_map.c.
 */
/** @{ reqhkey */

M0_INTERNAL unsigned m0_reqh_key_init(void);
M0_INTERNAL void *m0_reqh_key_find(struct m0_reqh *reqh, unsigned key,
				   m0_bcount_t size);
M0_INTERNAL void m0_reqh_key_fini(struct m0_reqh *reqh, unsigned key);

/** @} reqhkey */

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
