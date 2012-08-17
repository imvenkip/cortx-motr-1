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

#ifndef __COLIBRI_CM_CM_H__
#define __COLIBRI_CM_CM_H__

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
   - The c2_cm_aggr_group represents an aggregation group.
   - The c2_cm_aggr_group_ops defines the operations supported on an aggregation
     group.
   - The c2_cm_stats keeps copy machine operation progress data.
   - The c2_cm_sw is used for co-operation among copy machine replicas.

   @subsection CMDLD-fspec-if Interfaces
   Every copy machine type implements its own set of routines for
   type-specific operations, although there may exist few operations common
   to all the copy machine types.

   @subsection CMDLD-fspec-sub-cons Constructors and Destructors
   This section describes the sub-routines which act as constructors and
   destructors for various copy machine related data structures.

   - c2_cm_init()                    Initialises a copy machine.
   - c2_cm_fini()                    Finalises a copy machine.
   - c2_cm_start()                   Starts a copy machine.
   - C2_CM_TYPE_DECLARE()            Declares a copy machine type.

   @subsection CMDLD-fspec-sub-acc Accessors and Invariants
   The invariants would be implemented in source files.

   @subsection CMDLD-fspec-sub-opi Operational Interfaces
   Lists the various external interfaces exported by the copy machine.
   - c2_cm_configure()		 Fetches configuration from confc and configures
				 a copy machine.
   - c2_cm_failure_handle()	 Handles a copy machine failure.
   - c2_cm_done()		 Performs copy machine operation fini tasks.
   - c2_cm_operation_abort()	 Aborts a current ongoing copy machine
				 operation.

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
   compressing, re-integrating, etc.).

   @{
*/

#include "lib/ext.h"
#include "lib/tlist.h"  /* struct c2_tlink */
#include "addb/addb.h"  /* struct c2_addb_ctx */
#include "sm/sm.h"	/* struct c2_sm */
#include "cm/cp.h"
#include "cm/sw.h"
#include "net/buffer_pool.h" /* struct c2_net_buffer_pool */
#include "reqh/reqh_service.h" /* struct c2_reqh_service_type */
#include "cm/ag.h" /* struct c2_cm_aggr_group */

/* Import */
struct c2_fop;

/* Forward declarations */
struct c2_cm_sw;

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

/** Various copy machine related error codes. */
enum c2_cm_rc {
	C2_CM_SUCCESS,
	/** Copy machine start failure */
	C2_CM_ERR_START,
	/** Copy machine configuration failure. */
	C2_CM_ERR_CONF,
	/** Copy machine operational failure. */
	C2_CM_ERR_OP,
	C2_CM_NR
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
	 * State machine group for this copy machine type.
	 * Each replica uses the mutex embedded in their state machine group to
	 * serialise their state transitions and operations (ct_sm_group.s_lock)
	 */
	struct c2_sm_group		 cm_sm_group;

	/**
	 * Copy machine id. Copy machines are identified by this id.
	 * Copy machines can be located with this id by querying some
	 * configuration information.
	 */
	uint64_t                         cm_id;

	/** Copy machine operations. */
	const struct c2_cm_ops          *cm_ops;

	/** Request handler service instance this copy machine belongs to. */
	struct c2_reqh_service           cm_service;

	/** Copy machine type, this copy machine is an instance of. */
	const struct c2_cm_type         *cm_type;

	/** ADDB context to log important events and failures. */
	struct c2_addb_ctx               cm_addb;

	/** Sliding window controlled by this copy machine. */
	struct c2_cm_sw                  cm_sw;

	/**
         * Set true when copy machine shutdown triggered. Every finalisation
         * operation should check this flag.
	 */
	bool				 cm_shutdown;
};

/** Operations supported by a copy machine. */
struct c2_cm_ops {
	/** Invoked from generic c2_cm_start ().*/
	int (*cmo_start)(struct c2_cm *cm);

	/** Configures copy machine. */
	int (*cmo_config)(struct c2_cm *cm);

	/** Acknowledges the completion of copy machine operation. */
	void (*cmo_done)(struct c2_cm *cm);

	/** Invoked from c2_cm_stop (). */
	void (*cmo_stop)(struct c2_cm *cm);

	/** Creates copy packets after consulting sliding window. */
	struct c2_cm_cp *(cmo_cp_alloc)(struct c2_cm *cm);

	/** Copy machine specific finalisation routine. */
	void (*cmo_fini)(struct c2_cm *cm);
};

/**
 * Represents resource usage and copy machine operation progress
 * 0  : resource/operation is not used/complete at all.
 * 100: resource/operation is used/complete entirely.
 * 0 < value < 100: some fraction of resources/operation is used/complete.
 */
struct c2_cm_stats {
	/** Total Progress of copy machine operation. */
	int       s_progress;
	/** Start time of copy machine operation. */
	c2_time_t s_start;
	/** End time of copy machine operation. */
	c2_time_t s_end;
	/** Input set completion status. */
	int       s_iset;
	/** Output set completion status. */
	int       s_oset;
	/** Memory usage. */
	int       s_memory;
	/** CPU usage. */
	int       s_cpu;
	/** Network bandwidth usage. */
	int       s_network;
	/** Disk bandwidth usage. */
	int       s_disk;
};

int c2_cm_type_register(struct c2_cm_type *cmt);
void c2_cm_type_deregister(struct c2_cm_type *cmt);

/**
 * Locks copy machine replica. We use a state machine group per copy machine
 * replica.
 */
void c2_cm_group_lock(struct c2_cm *cm);

/** Releases the lock over a copy machine replica. */
void c2_cm_group_unlock(struct c2_cm *cm);

/**
 * Returns true, iff the copy machine lock is held by the current thread.
 * The lock should be released before returning from a fom state transition
 * function
 */
bool c2_cm_group_is_locked(struct c2_cm *cm);

int c2_cms_init(void);
void c2_cms_fini(void);

/**
 * Initialises a Copy machine. This is invoked from copy machine specific
 * service init routine.
 * Transitions copy machine into C2_CMS_INIT state if the initialisation
 * completes without any errors.
 * @pre cm != NULL;
 */
int c2_cm_init(struct c2_cm *cm, struct c2_cm_type *cm_type,
	       const struct c2_cm_ops *cm_ops,
	       const struct c2_cm_sw_ops *sw_ops);

/**
 * Finalises a copy machine. This is invoked from copy machine specific
 * service fini routine.
 * @pre cm != NULL && cm->cm_mach.sm_state == C2_CMS_IDLE;
 * @post c2_cm_state == C2_CMS_FINI;
 */
void c2_cm_fini(struct c2_cm *cm);

/**
 * Starts the copy machine data restructuring process on receiving the "POST"
 * fop. Internally invokes copy machine specific start routine.
 * In case of SNS repair, enough copy packets are created to populate the
 * sliding window by the copy machine specific service start routine.
 */
int c2_cm_start(struct c2_cm *cm);

/** Invokes copy machine specific stop routine (->cmo_stop()). */
void c2_cm_stop(struct c2_cm *cm);

/**
 * Configures a copy machine replica.
 * @pre C2_IN(cm->cm_mach.sm_state,(C2_CMS_IDLE, C2_CMS_DONE));
 */
int c2_cm_configure(struct c2_cm *cm, struct c2_fop *fop);

/**
 * Marks copy machine operation as complete. Transitions copy machine into
 * C2_CMS_IDLE.
 */
int c2_cm_done(struct c2_cm *cm);

/**
 * Handles various type of copy machine failures based on the failure code.
 * In case of non-recoverable failure (eg: copy machine init failure),
 * it transitions the copy machine to C2_CMS_UNDEFINED state. In case of other
 * recoverable failures (configuration failure, restructuring failure) the
 * current operation aborts.
 */
int c2_cm_failure_handle(struct c2_cm *cm);

#define C2_CM_TYPE_DECLARE(cmtype, ops, name)     \
struct c2_cm_type cmtype ## _cmt = {              \
	.ct_stype = {                             \
		.rst_name  = (name),              \
		.rst_ops   = (ops),               \
	}				          \
}					          \

/** Checks consistency of copy machine. */
bool c2_cm_invariant(struct c2_cm *cm);

/** Copy machine state mutators & accessors */
void c2_cm_state_set(struct c2_cm *cm, int state);
int  c2_cm_state_get(struct c2_cm *cm);

/** @} endgroup cm */

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
