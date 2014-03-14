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
 *		    Subhash Arya  <subhash_arya@xyratex.com>
 * Original creation date: 11/11/2011
 */

#pragma once

#ifndef __MERO_CM_CM_H__
#define __MERO_CM_CM_H__

#include "lib/tlist.h"         /* struct m0_tlink */

#include "addb/addb.h"         /* struct m0_addb_ctx */
#include "reqh/reqh_service.h" /* struct m0_reqh_service_type */
#include "sm/sm.h"	       /* struct m0_sm */
#include "fop/fom.h"           /* struct m0_fom */
#include "fop/fom_simple.h"

#include "cm/sw.h"
#include "cm/ag.h"
#include "cm/pump.h"

/**
   @page CMDLD-fspec Copy Machine Functional Specification

   - @ref CMDLD-fspec-ds
   - @ref CMDLD-fspec-if
   - @ref CMDLD-fspec-sub-cons
   - @ref CMDLD-fspec-sub-acc
   - @ref CMDLD-fspec-sub-opi
   - @ref CMDLD-fspec-usecases

   @section CMDLD-fspec Functional Specification

   @subsection CMDLD-fspec-ds Data Structures

   - The m0_cm represents a copy machine replica.
   - The m0_cm_ops provides copy machine specific routines for
	- Starting a copy machine.
	- Handling a copy machine specific operation.
	- Handling copy machine operation completion.
	- Aborting a copy machine operation.
	- Handling a copy machine failure.
	- Stopping a copy machine.
   - The m0_cm_type represents a copy machine type that a copy machine is an
     instance of.

   @subsection CMDLD-fspec-if Interfaces
   Every copy machine type implements its own set of routines for type-specific
   operations, although there may exist few operations common to all the copy
   machine types.

   @subsection CMDLD-fspec-sub-cons Constructors and Destructors
   This section describes the sub-routines which act as constructors and
   destructors for various copy machine related data structures.

   - m0_cm_init()                    Initialises a copy machine.
   - m0_cm_fini()                    Finalises a copy machine.
   - m0_cm_type_register()           Registers a new copy machine type.
   - m0_cm_type_deregister()         Deregisters a new copy machine type.
   - M0_CM_TYPE_DECLARE()            Declares a copy machine type.

   @subsection CMDLD-fspec-sub-acc Accessors and Invariants
   The invariants would be implemented in source files.

   @subsection CMDLD-fspec-sub-opi Operational Interfaces
   Lists the various external interfaces exported by the copy machine.
   - m0_cm_setup()		     Setup a copy machine.
   - m0_cm_prepare()                 Initialises local sliding window.
   - m0_cm_ready()                   Synchronizes copy machine with remote
				     replicas.
   - m0_cm_start()                   Starts copy machine operation.
   - m0_cm_fail()		     Handles a copy machine failure.
   - m0_cm_stop()		     Completes and aborts a copy machine
                                     operation.

   @subsection CMDLD-fspec-sub-opi-ext External operational Interfaces
   @todo This would be re-written when configuration api's would be implemented.
   - m0_confc_open()		   Opens an individual confc object.
				   processing.

   @section CMDLD-fspec-usecases Recipes
   @todo This section would be re-written when the other copy machine
   functionalities would be implemented.
 */

/**
   @defgroup CM Copy Machine

   Copy machine is a replicated state machine to restructure data in various
   ways (e.g. copying, moving, re-striping, reconstructing, encrypting,
   compressing, reintegrating, etc.).

   @{
*/

/* Import */
struct m0_fop;
struct m0_net_buffer_pool;

/**
 * Copy machine states.
 * @see The @ref CMDLD-lspec-state
 */
enum m0_cm_state {
	M0_CMS_INIT,
	M0_CMS_IDLE,
	M0_CMS_PREPARE,
	M0_CMS_READY,
	M0_CMS_ACTIVE,
	M0_CMS_FAIL,
	M0_CMS_STOP,
	M0_CMS_FINI,
	M0_CMS_NR
};

/**
 * Various copy machine failures. m0_cm_fail() uses these to perform failure
 * specific processing like sending ADDB messages etc.
 * @see m0_cm_fail()
 */
enum m0_cm_failure {
	/** Copy machine setup failure */
	M0_CM_ERR_SETUP = 1,
	M0_CM_ERR_PREPARE,
	/** Copy machine start failure */
	M0_CM_ERR_READY,
	M0_CM_ERR_START,
	/** Copy machine stop failure */
	M0_CM_ERR_STOP,
	M0_CM_ERR_NR
};

enum {
	CM_RPC_TIMEOUT              = 30, /* seconds */
	CM_MAX_NR_RPC_IN_FLIGHT     = 100,
};

/** Copy Machine type, implemented as a request handler service. */
struct m0_cm_type {
	/** Service type corresponding to this copy machine type. */
	struct m0_reqh_service_type   ct_stype;
	/** Linkage into the list of copy machine types (struct m0_tl cmtypes)*/
	struct m0_tlink               ct_linkage;
	uint64_t                      ct_magix;
};

/** Copy machine replica. */
struct m0_cm {
	struct m0_sm			 cm_mach;

        /** Pool machine for this node. */
        struct m0_poolmach              *cm_pm;

	/**
	 * Copy machine id. Copy machines are identified by this id.
	 * Copy machines can be located with this id by querying some
	 * configuration information.
	 */
	uint64_t                         cm_id;

	/**
	 * State machine group for this copy machine type.
	 * Each replica uses the mutex embedded in their state machine group to
	 * serialise their state transitions and operations (cm_sm_group.s_lock)
	 * .
	 */
	struct m0_sm_group		 cm_sm_group;

	/** Copy machine operations. */
	const struct m0_cm_ops          *cm_ops;

	/** Request handler service instance this copy machine belongs to. */
	struct m0_reqh_service           cm_service;

	/** Copy machine type, this copy machine is an instance of. */
	const struct m0_cm_type         *cm_type;

	/**
	 * List of aggregation groups having incoming copy packets for this copy
	 * machine replica.
	 * Copy machine provides various interfaces over this list to implement
	 * sliding window.
	 * @see struct m0_cm_aggr_group::cag_cm_out_linkage
	 */
	struct m0_tl                     cm_aggr_grps_in;

	uint64_t                         cm_aggr_grps_in_nr;

	/**
	 * Saved aggregation group identifier for the last processed
	 * aggregation group with the highest identifier in the sliding window.
	 * This is mainly referred while advancing the sliding window.
	 * This also resolves the issue, where an aggregation group with the
	 * highest identifier in the sliding window was just finalised and
	 * sliding window could not advance before that due to unavailability of
	 * buffers. Thus in this case, there's a possibility of allocating an
	 * aggregation group with the previously processed group identifier
	 * during the later sliding window updates. Thus saving the highest
	 * processed aggregation group identifier from the sliding window
	 * avoids this situation.
	 */
	struct m0_cm_ag_id               cm_last_saved_sw_hi;

	/**
	 * List of aggregation groups having outgoing copy packets from this
	 * copy machine replica.
	 * @see struct m0_cm_aggr_group::cag_cm_out_linkage
	 */
	struct m0_tl                     cm_aggr_grps_out;

	uint64_t                         cm_aggr_grps_out_nr;

	struct m0_chan                   cm_ready_wait;
	struct m0_chan                   cm_complete_wait;
	struct m0_mutex                  cm_wait_mutex;

	struct m0_fom_simple             cm_ast_run_fom;
	struct m0_chan                   cm_ast_run_fom_wait;
	struct m0_mutex                  cm_ast_run_fom_wait_mutex;

	/**
	 * Counter to track number of ready fops received from other replicas.
	 * Once the m0_cm::cm_ready_fops_recvd ==
	 * proxy_tlist_length(m0_cm::cm_proxies), then the copy machine
	 * transitions to next phase, i.e. M0_CMS_ACTIVE.
	 */
	uint64_t                         cm_ready_fops_recvd;

	/**
	 * List of m0_cm_proxy objects representing remote replicas.
	 * @see struct m0_cm_proxy::px_linkage
	 */
	struct m0_tl                     cm_proxies;

	uint64_t                         cm_proxy_nr;

	/** Copy packet pump FOM for this copy machine. */
	struct m0_cm_cp_pump             cm_cp_pump;

	struct m0_cm_sw_update           cm_sw_update;
};

/** Operations supported by a copy machine. */
struct m0_cm_ops {
	/**
	 * Initialises copy machine specific data structures.
	 * This is invoked from generic m0_cm_setup() routine. Once the copy
	 * machine is setup successfully it transitions into M0_CMS_IDLE state.
	 */
	int (*cmo_setup)(struct m0_cm *cm);

	int (*cmo_prepare)(struct m0_cm *cm);

	/**
	 * Starts copy machine operation. Acquires copy machine specific
	 * resources, broadcasts READY FOPs and starts copy machine operation
	 * based on the TRIGGER event.
	 */
	int (*cmo_start)(struct m0_cm *cm);

	/** Invoked from m0_cm_stop(). */
	int (*cmo_stop)(struct m0_cm *cm);

	int (*cmo_ag_alloc)(struct m0_cm *cm, const struct m0_cm_ag_id *id,
			    bool has_incoming, struct m0_cm_aggr_group **out);

	/** Creates copy packets only if resources permit. */
	struct m0_cm_cp *(*cmo_cp_alloc)(struct m0_cm *cm);

	/**
	 * Iterates over the copy machine data set and populates the copy packet
	 * with meta data of next data object to be restructured, i.e. fid,
	 * aggregation group, &c.
	 * Also attaches data buffer to m0_cm_cp::c_data, if successful.
	 */
	int (*cmo_data_next)(struct m0_cm *cm, struct m0_cm_cp *cp);

	/**
	 * Calculates next relevant aggregation group id and returns it in
	 * the "id_next".
	 */
	int (*cmo_ag_next)(struct m0_cm *cm,
			   const struct m0_cm_ag_id id_curr,
			   struct m0_cm_ag_id *id_next);

	/**
	 * Returns true if copy machine has enough buffers for all the incoming
	 * copy packets.
	 */
	bool (*cmo_has_space)(struct m0_cm *cm, const struct m0_cm_ag_id *id,
			      struct m0_layout *l);

	/**
	 * Initialises the given fop with copy machine specific sliding window
	 * update fop type and given information.
	 */
	int (*cmo_sw_onwire_fop_setup)(struct m0_cm *cm, struct m0_fop *fop,
				       void (*fop_release)(struct m0_ref *),
				       const char *local_ep,
				       const struct m0_cm_sw *sw);

	/** Copy machine specific finalisation routine. */
	void (*cmo_fini)(struct m0_cm *cm);
};

M0_INTERNAL int m0_cm_type_register(struct m0_cm_type *cmt);
M0_INTERNAL void m0_cm_type_deregister(struct m0_cm_type *cmt);

/**
 * Locks copy machine replica. We use a state machine group per copy machine
 * replica.
 */
M0_INTERNAL void m0_cm_lock(struct m0_cm *cm);
M0_INTERNAL int m0_cm_trylock(struct m0_cm *cm);

/** Releases the lock over a copy machine replica. */
M0_INTERNAL void m0_cm_unlock(struct m0_cm *cm);

/**
 * Returns true, iff the copy machine lock is held by the current thread.
 * The lock should be released before returning from a fom state transition
 * function. This function is used only in assertions.
 */
M0_INTERNAL bool m0_cm_is_locked(const struct m0_cm *cm);

M0_INTERNAL int m0_cm_module_init(void);
M0_INTERNAL void m0_cm_module_fini(void);

/**
 * Initialises a copy machine. This is invoked from copy machine specific
 * service init routine.
 * Transitions copy machine into M0_CMS_INIT state if the initialisation
 * completes without any errors.
 * @pre cm != NULL
 * @post ergo(result == 0, m0_cm_state_get(cm) == M0_CMS_INIT)
 */
M0_INTERNAL int m0_cm_init(struct m0_cm *cm, struct m0_cm_type *cm_type,
			   const struct m0_cm_ops *cm_ops);

/**
 * Finalises a copy machine. This is invoked from copy machine specific
 * service fini routine.
 * @pre cm != NULL && m0_cm_state_get(cm) == M0_CMS_IDLE
 * @post m0_cm_state_get(cm) == M0_CMS_FINI
 */
M0_INTERNAL void m0_cm_fini(struct m0_cm *cm);

/**
 * Perfoms copy machine setup tasks by calling copy machine specific setup
 * routine. This is invoked from copy machine specific service start routine.
 * On successful completion of the setup, a copy machine transitions to "IDLE"
 * state where it waits for a data restructuring request.
 * @pre cm != NULL && m0_cm_state_get(cm) == M0_CMS_INIT
 * @post m0_cm_state_get(cm) == M0_CMS_IDLE
 */
M0_INTERNAL int m0_cm_setup(struct m0_cm *cm);

M0_INTERNAL int m0_cm_prepare_init(struct m0_cm *cm);
M0_INTERNAL int m0_cm_prepare_done(struct m0_cm *cm);
M0_INTERNAL int m0_cm_prepare_sw_store_init(struct m0_cm *cm,
					    struct m0_sm_group *grp);
M0_INTERNAL int m0_cm_prepare_sw_store_commit(struct m0_cm *cm);
M0_INTERNAL void m0_cm_prepare_sw_store_fini(struct m0_cm *cm);
M0_INTERNAL int m0_cm_ready(struct m0_cm *cm);

M0_INTERNAL bool m0_cm_is_ready(struct m0_cm *cm);
M0_INTERNAL bool m0_cm_is_active(struct m0_cm *cm);

/**
 * Starts the copy machine data restructuring process on receiving the "POST"
 * fop. Internally invokes copy machine specific start routine.
 * Starts pump FOM.
 * @pre cm != NULL && m0_cm_state_get(cm) == M0_CMS_IDLE
 * @post m0_cm_state_get(cm) == M0_CMS_ACTIVE
 */
M0_INTERNAL int m0_cm_start(struct m0_cm *cm);

/**
 * Stops copy machine operation.
 * Once operation completes successfully, copy machine performs required tasks,
 * (e.g. updating layouts, cleanup, etc.) by invoking m0_cm_stop(), this
 * transitions copy machine back to M0_CMS_IDLE state. Copy machine invokes
 * m0_cm_stop() also in case of operational failure to broadcast STOP FOPs to
 * its other replicas in the pool, indicating failure. This is handled specific
 * to the copy machine type.
 * @pre cm!= NULL && M0_IN(m0_cm_state_get(cm), (M0_CMS_ACTIVE))
 * @post M0_IN(m0_cm_state_get(cm), (M0_CMS_IDLE, M0_CMS_FAIL))
 */
M0_INTERNAL int m0_cm_stop(struct m0_cm *cm);

/**
 * Configures a copy machine replica.
 * @todo Pass actual configuration fop data structure once configuration
 * interfaces and datastructures are available.
 * @pre m0_cm_state_get(cm) == M0_CMS_IDLE
 */
M0_INTERNAL int m0_cm_configure(struct m0_cm *cm, struct m0_fop *fop);

/**
 * Handles various type of copy machine failures based on the failure code and
 * errno.
 * Currently, all this function does is send failure specific addb events and
 * sets corresponding m0_sm->sm_rc. A better implementation would be creating
 * a failure descriptor table based on various failures which would contain
 * an ADDB event for each failure and an "failure_action" op.
 * However, due to limitations in the current ADDB infrastructure, this is not
 * feasible.
 *
 * @todo Rewrite this function when new ADDB infrastucture is in place.
 * @param cm Failed copy machine.
 * @param failure Copy machine failure code.
 * @param rc errno to which sm rc will be set to.
 */
M0_INTERNAL void m0_cm_fail(struct m0_cm *cm, enum m0_cm_failure failure,
			    int rc);

#define M0_CM_TYPE_DECLARE(cmtype, ops, name, ct) \
struct m0_cm_type cmtype ## _cmt = {                  \
	.ct_stype = {                                 \
		.rst_name    = (name),                \
		.rst_ops     = (ops),                 \
		.rst_addb_ct = (ct),                  \
	}				              \
}					              \

/** Checks consistency of copy machine. */
M0_INTERNAL bool m0_cm_invariant(const struct m0_cm *cm);

/** Copy machine state mutators & accessors */
M0_INTERNAL void m0_cm_state_set(struct m0_cm *cm, enum m0_cm_state state);
M0_INTERNAL enum m0_cm_state m0_cm_state_get(const struct m0_cm *cm);

/**
 * Creates copy packets and adds aggregation groups to m0_cm::cm_aggr_grps,
 * if required.
 */
M0_INTERNAL void m0_cm_continue(struct m0_cm *cm);

/**
 * Iterates over data to be re-structured.
 *
 * @pre m0_cm_invariant(cm)
 * @pre m0_cm_is_locked(cm)
 * @pre cp != NULL
 *
 * @post ergo(rc == 0, cp->c_data != NULL)
 */
M0_INTERNAL int m0_cm_data_next(struct m0_cm *cm, struct m0_cm_cp *cp);

/**
 * Checks if copy machine pump FOM will be creating more copy packets or if
 * its done. Once pump FOM is done creating copy packets, it sets
 * m0_cm_cp_pump::p_fom.fo_sm_phase.sm_rc = -ENODATA, the same is checked by
 * this function.
 */
M0_INTERNAL bool m0_cm_has_more_data(const struct m0_cm *cm);

M0_INTERNAL struct m0_net_buffer *m0_cm_buffer_get(struct m0_net_buffer_pool
						   *bp, uint64_t colour);

M0_INTERNAL void m0_cm_buffer_put(struct m0_net_buffer_pool *bp,
				  struct m0_net_buffer *buf,
				  uint64_t colour);

M0_INTERNAL struct m0_cm *m0_cmsvc2cm(struct m0_reqh_service *cmsvc);

M0_INTERNAL void m0_cm_proxies_fini(struct m0_cm *cm);

M0_INTERNAL struct m0_rpc_machine *m0_cm_rpc_machine_find(struct m0_reqh *reqh);

M0_INTERNAL void m0_cm_ast_run_fom_wakeup(struct m0_cm *cm);
M0_INTERNAL void m0_cm_ready_done(struct m0_cm *cm);
M0_INTERNAL void m0_cm_complete(struct m0_cm *cm);

/** @} endgroup CM */

/* __MERO_CM_CM_H__ */

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
