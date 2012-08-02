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
   @page DLD-cm-fspec Copy Machine Functional Specification

   - @ref DLD-cm-fspec-ds
   - @ref DLD-cm-fspec-if
   - @ref DLD-cm-fspec-sub-cons
   - @ref DLD-cm-fspec-sub-acc
   - @ref DLD-cm-fspec-sub-opi
   - @ref DLD-cm-fspec-usecases

   @section DLD-cm-fspec Functional Specification

   @subsection DLD-cm-fspec-ds Data Structures

   - The c2_cm represents a copy machine replica.
   The c2_cm_ops provides copy machine specific routines for
	- Starting a copy machine.
	- Handling a copy machine specific operation.
	- Handling copy machine operation completion.
	- Aborting a copy machine operation.
	- Handling a copy machine failure or an agent failure.
	- Stopping a copy machine.
	- The c2_cm_cb provides notification call-backs to be invoked at
          different granularities like
	- Updates to the sliding window.
	- Agent failure.
   - The c2_cm_aggr_group represents an aggregation group.
   - The c2_cm_aggr_group_ops defines the operations supported on an aggregation
   group.
   - The c2_cm_stats keeps copy machine operation progress data.
   - The c2_cm_sw is used for co-operation among agents.

   @subsection DLD-cm-fspec-if Interfaces
   Every copy machine type implements its own set of routines for
   type-specific operations, although there may exist few operations common
   to all the copy machine types.

   @subsection DLD-cm-fspec-sub-cons Constructors and Destructors
   This section describes the sub-routines which act as constructors and
   destructors for various copy machine related data structures.

   - c2_cm_init()                    Initialises a copy machine.
   - c2_cm_fini()                    Finalises a copy machine.
   - c2_cm_start()                   Starts a copy machine and corresponding
				     agents.
   - C2_CM_TYPE_DECLARE()            Declares a copy machine type.

   @subsection DLD-cm-fspec-sub-acc Accessors and Invariants
   The invariants would be implemented in source files.

   @subsection DLD-cm-fspec-sub-opi Operational Interfaces
   Lists the various external interfaces exported by the copy machine.
   - c2_cm_configure()		 Fetches configuration from confc and configures
				 a copy machine.
   - c2_cm_aggr_group_locate()	 Locates an aggregation group based on
				 aggregation group id.
   - c2_cm_failure_handle()	 Handles a copy machine failure.
   - c2_cm_done()		 Performs copy machine operation fini tasks.
   - c2_cm_operation_abort()	 Aborts a current ongoing copy machine
				 operation.

   @subsection DLD-cm-fspec-sub-opi-ext External operational Interfaces
   @todo This would be re-written when configuration api's would be implemented.
   - c2_confc_open()		   Opens an individual confc object.
				   processing.

   @section DLD-cm-fspec-usecases Recipes
   @todo This section would be re-written when the other copy machine
   functionalities would be implemented.
 */

/**
   @defgroup cm Copy Machine

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

/* Import */
struct c2_fop;

/* Forward declarations */
struct c2_cm_sw;
struct c2_cm_aggr_group;

/**
 * Copy machine states.
 * @see The @ref DLD-cm-lspec-state
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


/** Aggregation group states */
enum c2_aggr_group_state {
	/**
	 * Aggregation group data structure is initialised and is ready for
	 * processing.
	 */
	C2_AGS_INITIALISED,
	/**
	 * The aggregation group is being processed by the agents in the
	 * pipeline.
	 */
	C2_AGS_IN_PROCESS,
	/** The restructuring for this aggregation group has been completed. */
	C2_AGS_FINALISED
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
	/** Copy machine agent failure. */
	C2_CM_ERR_AGENT,
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

	/** Callback handlers for this copy machine. */
	const struct c2_cm_cb           *cm_cb;

	/** Sliding window controlled by this copy machine. */
	struct c2_cm_sw                  cm_sw;

	/**
         * Set true when copy machine shutdown triggered. Every finalisation
         * operation should check this flag.
	 */
	bool				 cm_shutdown;
};

/**
 * Notification callbacks that will be invoked at various granularities like
 * - Completion of processing of an aggregation group, container, device and
 *   a pool server.
 * - Updates to the sliding window.
 */
struct c2_cm_cb {
	/** @todo Adding comments. */
	void (*cmcb_container)(struct c2_cm *cm, uint64_t cid);

	/** @todo Adding comments. */
	void (*cmcb_device)(struct c2_cm *cm, uint64_t devid);

	/** @todo Adding comments. */
	void (*cmcb_sl_window)(struct c2_cm *cm, struct c2_cm_sw *sw);
};

/** Operations supported by a copy machine. */
struct c2_cm_ops {
	/**
	 * Invoked from generic c2_cm_start ().
	 */
	int (*cmo_start)(struct c2_cm *cm);

	/** Configures copy machine and its corresponding agents. */
	int (*cmo_config)(struct c2_cm *cm);

	/**
	 * Gets the next agent for this copy packet.
	 *
	 * The result agent may be a local agent on this node, but also
	 * might be remote agent on other node.
	 * Configuration information and layout information will be used
	 * to find the next agent in the copy packet pipeline.
	 *
	 * @param cm this copy machine.
	 * @param packet the current packet.
	 * @param current_agent current agent.
	 * @param next_agent_id [out] the next agent id returned.
	 */
	int (*cmo_next_agent)(struct c2_cm          *cm,
			      struct c2_cm_cp       *packet);

	/**
	 * Transformation function.
	 *
	 * Transforms the incoming copy packet to create a list of transformed
	 * copy packets.
	 *
	 * This is done by extracting the aggregation group to which the
	 * incoming copy packet belongs and then by updating the aggregation
	 * group type c2_cm_aggr_group_type corresponding to the aggregation
	 * group. Aggregation group type keeps track of list of transformed
	 * copy packets along with any copy machine type intermediate
	 * information that is needed during transformation.
	 *
	 * For example: In case of SNS repair, the idea of transformation
	 * function is to take all the copy packets belonging to an aggregation
	 * group and transform them into a single copy packet. This involves
	 * XORing of the c2_net_buffer's corresponding to all the copy packets
	 * and creating a single c2_net_buffer which belongs to a new outgoing
	 * copy packet.
	 *
	 * Tranformation function is called in context of a collecting agent
	 * fom. Collecting agent uses some mechanism to make sure that all the
	 * copy packets belonging to the aggregation group corresponding to the
	 * incoming copy packet are transformed. This mechanism is copy machine
	 * type specific.
	 *
	 * @param packet Copy packet that should be transformed by the
	 * collecting agent.
	 * @pre packet->cp_state == WAIT_COLLECT
	 */
	int (*cmo_transform)(struct c2_cm_cp *packet);

	/**
	 * Handles incoming request fop and performs copy machine
	 * specific operations.
	 */
	int (*cmo_incoming)(struct c2_cm *cm, struct c2_fom *fom);

	/** Acknowledges the completion of copy machine operation. */
	void (*cmo_done)(struct c2_cm *cm);

	/** Invoked from c2_cm_stop (). */
	void (*cmo_stop)(struct c2_cm *cm);

	/** Copy machine specific finalisation routine. */
	void (*cmo_fini)(struct c2_cm *cm);
};

/** Copy Machine Aggregation Group. */
struct c2_cm_aggr_group {
	/** Parent copy machine. */
	struct c2_cm                      *cag_cm;

	/** Aggregation group id */
	struct c2_uint128		   cag_id;

	/** Aggregation state. */
	enum c2_aggr_group_state           cag_state;

	/** Input set reference. */
	struct c2_cm_ioset                *cag_iset;

	/** Output set reference. */
	struct c2_cm_ioset                *cag_oset;

	/** Its operations. */
	const struct c2_cm_aggr_group_ops *cag_ops;

	/**
	 * Linkage into the sorted sliding window queue of aggregation group
	 * ids, Hanging to c2_cm_sw::sw_aggr_grps.
	 */
	struct c2_tlink			   cag_sw_linkage;

	/** List of copy packets belonging to this group. */
	struct c2_tl                       cag_cpl;
	uint64_t                           cag_magic;

	/** Mutex lock to protect this group. */
	struct c2_mutex                    cag_lock;

	/** Number of copy packets that correspond to this aggregation group. */
	uint64_t                           cag_cp_nr;

	/** Number of copy packets that are transformed. */
	uint64_t                           cag_transformed_cp_nr;

};

/** Colibri Copy Machine Aggregation Group Operations */
struct c2_cm_aggr_group_ops {
	/** Returns extent from the input set matching to this group. */
	int (*cago_get)(struct c2_cm_aggr_group *ag, struct c2_ext *ext_out);

	/** Aggregation group processing completion notification. */
	int (*cago_completed)(struct c2_cm_aggr_group *ag);

	/**
	 * Returns number of copy packets corresponding to the aggregation
	 * group on the local node. Typically this is calculated as,
	 * number of data units per node * unit size / network buffer size.
	 */
	uint64_t (*cago_cp_nr)(struct c2_cm_aggr_group *ag);
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
	       const struct c2_cm_ops *cm_ops, const struct c2_cm_cb *cb_ops,
	       const struct c2_cm_sw_ops *sw_ops);

/**
 * Finalises a copy machine. This is invoked from copy machine specific
 * service fini routine.
 * @pre cm != NULL && cm->cm_mach.sm_state == C2_CMS_IDLE;
 * @post c2_cm_state == C2_CMS_FINI;
 */
void c2_cm_fini(struct c2_cm *cm);

/**
 * Invokes copy machine service specific start routine, creates service
 * specific instance containing c2_reqh_service, invokes service type specific
 * implementation of service alloc and init () operation. Also, builds copy
 * machine specific fop types, creates and starts specific agents and
 * initialises service buffer pool.
 */
int c2_cm_start(struct c2_cm *cm);

/** Invokes copy machine specific stop routine (->cmo_stop()). */
void c2_cm_stop(struct c2_cm *cm);

/**
 * Configures copy machine agents.
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

bool c2_cm_service_is_cm(struct c2_reqh_service *service);

/** Copy machine state mutators & accessors */
void c2_cm_state_set(struct c2_cm *cm, int state);
int  c2_cm_state_get(struct c2_cm *cm);

struct c2_chan *c2_cm_signal(struct c2_cm *cm);

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
