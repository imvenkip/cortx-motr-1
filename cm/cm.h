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

#ifndef __COLIBRI_CM_CM_H__
#define __COLIBRI_CM_CM_H__

#include "lib/tlist.h"         /* struct c2_tlink */

#include "addb/addb.h"         /* struct c2_addb_ctx */
#include "reqh/reqh_service.h" /* struct c2_reqh_service_type */
#include "sm/sm.h"	       /* struct c2_sm */

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

   - The c2_cm represents a copy machine replica.
   - The c2_cm_ops provides copy machine specific routines for
	- Starting a copy machine.
	- Handling a copy machine specific operation.
	- Handling copy machine operation completion.
	- Aborting a copy machine operation.
	- Handling a copy machine failure.
	- Stopping a copy machine.
   - The c2_cm_type represents a copy machine type that a copy machine is an
     instance of.

   @subsection CMDLD-fspec-if Interfaces
   Every copy machine type implements its own set of routines for type-specific
   operations, although there may exist few operations common to all the copy
   machine types.

   @subsection CMDLD-fspec-sub-cons Constructors and Destructors
   This section describes the sub-routines which act as constructors and
   destructors for various copy machine related data structures.

   - c2_cm_init()                    Initialises a copy machine.
   - c2_cm_fini()                    Finalises a copy machine.
   - c2_cm_type_register()           Registers a new copy machine type.
   - c2_cm_type_deregister()         Deregisters a new copy machine type.
   - C2_CM_TYPE_DECLARE()            Declares a copy machine type.

   @subsection CMDLD-fspec-sub-acc Accessors and Invariants
   The invariants would be implemented in source files.

   @subsection CMDLD-fspec-sub-opi Operational Interfaces
   Lists the various external interfaces exported by the copy machine.
   - c2_cm_setup()		     Setup a copy machine.
   - c2_cm_start()                   Starts copy machine operation.
   - c2_cm_fail()		     Handles a copy machine failure.
   - c2_cm_done()		     Performs copy machine operation fini tasks.

   @subsection CMDLD-fspec-sub-opi-ext External operational Interfaces
   @todo This would be re-written when configuration api's would be implemented.
   - c2_confc_open()		   Opens an individual confc object.
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
struct c2_fop;

/**
 * Copy machine states.
 * @see The @ref CMDLD-lspec-state
 */
enum c2_cm_state {
	C2_CMS_INIT,
	C2_CMS_IDLE,
	C2_CMS_READY,
	C2_CMS_ACTIVE,
	C2_CMS_FAIL,
	C2_CMS_DONE,
	C2_CMS_STOP,
	C2_CMS_FINI,
	C2_CMS_NR
};

/**
 * Various copy machine failures. c2_cm_fail() uses these to perform failure
 * specific processing like sending ADDB messages etc.
 * @see c2_cm_fail()
 */
enum c2_cm_failure {
	/** Copy machine setup failure */
	C2_CM_ERR_SETUP,
	/** Copy machine start failure */
	C2_CM_ERR_START,
	/** Copy machine stop failure */
	C2_CM_ERR_STOP,
	C2_CM_ERR_NR
};

/** Copy Machine type, implemented as a request handler service. */
struct c2_cm_type {
	/** Service type corresponding to this copy machine type. */
	struct c2_reqh_service_type   ct_stype;
	/** Linkage into the list of copy machine types (struct c2_tl cmtypes)*/
	struct c2_tlink               ct_linkage;
	uint64_t                      ct_magix;
};

/** Copy machine replica. */
struct c2_cm {
	struct c2_sm			 cm_mach;

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
	struct c2_sm_group		 cm_sm_group;

	/** Copy machine operations. */
	const struct c2_cm_ops          *cm_ops;

	/** Request handler service instance this copy machine belongs to. */
	struct c2_reqh_service           cm_service;

	/** Copy machine type, this copy machine is an instance of. */
	const struct c2_cm_type         *cm_type;

	/** ADDB context to log important events and failures. */
	struct c2_addb_ctx               cm_addb;

	/**
	 * List of aggregation groups in process.
	 * Copy machine provides various interfaces over this list to implement
	 * sliding window.
	 * @see struct c2_cm_aggr_group::cag_cm_linkage
	 */
	struct c2_tl                     cm_aggr_grps;
};

/** Operations supported by a copy machine. */
struct c2_cm_ops {
	/**
	 * Initialises copy machine specific data structures.
	 * This is invoked from generic c2_cm_setup() routine. Once the copy
	 * machine is setup successfully it transitions into C2_CMS_IDLE state.
	 */
	int (*cmo_setup)(struct c2_cm *cm);

	/**
	 * Starts copy machine operation. Acquires copy machine specific
	 * resources, broadcasts READY FOPs and starts copy machine operation
	 * based on the TRIGGER event.
	 */
	int (*cmo_start)(struct c2_cm *cm);

	/** Acknowledges the completion of copy machine operation. */
	void (*cmo_done)(struct c2_cm *cm);

	/** Invoked from c2_cm_stop(). */
	int (*cmo_stop)(struct c2_cm *cm);

	/** Creates copy packets only if resources permit. */
	struct c2_cm_cp *(*cmo_cp_alloc)(struct c2_cm *cm);

	/**
	 * Iterates over the copy machine data set and populates the copy packet
	 * with meta data of next data object to be restructured, i.e. fid,
	 * aggregation group, &c.
	 */
	int (*cmo_data_next)(struct c2_cm *cm, struct c2_cm_cp *cp);

	/** Copy machine specific finalisation routine. */
	void (*cmo_fini)(struct c2_cm *cm);
};

int c2_cm_type_register(struct c2_cm_type *cmt);
void c2_cm_type_deregister(struct c2_cm_type *cmt);

/**
 * Locks copy machine replica. We use a state machine group per copy machine
 * replica.
 */
void c2_cm_lock(struct c2_cm *cm);

/** Releases the lock over a copy machine replica. */
void c2_cm_unlock(struct c2_cm *cm);

/**
 * Returns true, iff the copy machine lock is held by the current thread.
 * The lock should be released before returning from a fom state transition
 * function. This function is used only in assertions.
 */
bool c2_cm_is_locked(const struct c2_cm *cm);

int c2_cms_init(void);
void c2_cms_fini(void);

/**
 * Initialises a Copy machine. This is invoked from copy machine specific
 * service init routine.
 * Transitions copy machine into C2_CMS_INIT state if the initialisation
 * completes without any errors.
 * @pre cm != NULL
 * @post ergo(result == 0, c2_cm_state_get(cm) == C2_CMS_INIT)
 */
int c2_cm_init(struct c2_cm *cm, struct c2_cm_type *cm_type,
	       const struct c2_cm_ops *cm_ops);

/**
 * Finalises a copy machine. This is invoked from copy machine specific
 * service fini routine.
 * @pre cm != NULL && c2_cm_state_get(cm) == C2_CMS_IDLE
 * @post c2_cm_state_get(cm) == C2_CMS_FINI
 */
void c2_cm_fini(struct c2_cm *cm);

/**
 * Perfoms copy machine setup tasks by calling copy machine specific setup
 * routine. This is invoked from copy machine specific service start routine.
 * On successful completion of the setup, a copy machine transitions to "IDLE"
 * state where it waits for a data restructuring request.
 * @pre cm != NULL && c2_cm_state_get(cm) == C2_CMS_INIT
 * @post c2_cm_state_get(cm) == C2_CMS_IDLE
 */
int c2_cm_setup(struct c2_cm *cm);

/**
 * Starts the copy machine data restructuring process on receiving the "POST"
 * fop. Internally invokes copy machine specific start routine.
 * @pre cm != NULL && c2_cm_state_get(cm) == C2_CMS_IDLE
 * @post c2_cm_state_get(cm) == C2_CMS_ACTIVE
 */
int c2_cm_start(struct c2_cm *cm);

/**
 * Stops the copy machine data restructuring process by sending the "STOP" fop.
 * Invokes copy machine specific stop routine (->cmo_stop()).
 * @pre cm!= NULL && C2_IN(c2_cm_state_get(cm), (C2_CMS_ACTIVE, C2_CMS_IDLE))
 * @post C2_IN(c2_cm_state_get(cm), (C2_CMS_IDLE, C2_CMS_FAIL, C2_CM_STOP))
 */
int c2_cm_stop(struct c2_cm *cm);

/**
 * Configures a copy machine replica.
 * @todo Pass actual configuration fop data structure once configuration
 * interfaces and datastructures are available.
 * @pre C2_IN(c2_cm_state_get(cm), (C2_CMS_IDLE, C2_CMS_DONE))
 */
int c2_cm_configure(struct c2_cm *cm, struct c2_fop *fop);

/**
 * Marks copy machine operation as complete. Transitions copy machine into
 * C2_CMS_IDLE.
 * @post c2_cm_state_get(cm) == C2_CMS_IDLE
 */
int c2_cm_done(struct c2_cm *cm);

/**
 * Handles various type of copy machine failures based on the failure code and
 * errno.
 * Currently, all this function does is send failure specific addb events and
 * sets corresponding c2_sm->sm_rc. A better implementation would be creating
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
void c2_cm_fail(struct c2_cm *cm, enum c2_cm_failure failure, int rc);

#define C2_CM_TYPE_DECLARE(cmtype, ops, name)     \
struct c2_cm_type cmtype ## _cmt = {              \
	.ct_stype = {                             \
		.rst_name  = (name),              \
		.rst_ops   = (ops),               \
	}				          \
}					          \

/** Checks consistency of copy machine. */
bool c2_cm_invariant(const struct c2_cm *cm);

/** Copy machine state mutators & accessors */
void c2_cm_state_set(struct c2_cm *cm, enum c2_cm_state state);
enum c2_cm_state c2_cm_state_get(const struct c2_cm *cm);

/**
 * Creates copy packets and adds aggregation groups to c2_cm::cm_aggr_grps,
 * if required.
 */
void c2_cm_ag_fill(struct c2_cm *cm);

/** Iterates over data to be re-structured. */
int c2_cm_data_next(struct c2_cm *cm, struct c2_cm_cp *cp);

/** Returns last element from the c2_cm::cm_aggr_grps list. */
struct c2_cm_aggr_group *c2_cm_ag_hi(struct c2_cm *cm);

/** Returns first element from the c2_cm::cm_aggr_grps list. */
struct c2_cm_aggr_group *c2_cm_ag_lo(struct c2_cm *cm);

/** @} endgroup CM */

/* __COLIBRI_CM_CM_H__ */

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
