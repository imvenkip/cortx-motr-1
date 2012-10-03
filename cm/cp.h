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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 02/22/2012
 */

#pragma once

#ifndef __COLIBRI_CM_CP_H__
#define __COLIBRI_CM_CP_H__

#include "lib/vec.h"

#include "fop/fom_generic.h"
#include "rpc/bulk.h"

/**
 * @page CPDLD-fspec Copy Packet Functional Specification
 *
 * - @ref CPDLD-fspec-ds
 * - @ref CPDLD-fspec-sub
 * - @ref CP "Copy Packet Functional Specification" <!-- Note link -->
 *
 * @section CPDLD-fspec-ds Data Structures
 *	- @b c2_cm_cp  : Generic copy packet.
 *	- @b c2_cm_ops : Copy packet operations.
 * @section CPDLD-fspec-sub Subroutines
 *	- @b c2_cm_cp_init() : Initialises copy packet members and calls
 *			       c_ops->init() to initialise specific data.
 *	- @b c2_cm_cp_fini() : Finalises copy packet members and calls
 *			       c_ops->fini() internal to finalise specific data.
 *	- @b c2_cm_cp_enqueue() : Posts copy packet FOM for execution.
 *
 * @subsection CPDLD-fspec-sub-acc Accessors and Invariants
 *	- @b c2_cm_cp_invaraint()
 *
 * @subsection CPDLD-fspec-sub-opi Operational Interfaces
 *	- @b cp_fom_fini()
 *	- @b cp_fom_locality()
 *	- @b cp_fom_state()
 *
 *	@see @ref CP
 */

/**
 * @defgroup CP Copy Packet
 * @ingroup CM
 *
 * @see The @ref CP "Copy Packet" its
 * @ref CPDLD-fspec "Copy Packet Functional Specification"
 *
 * @{
 */

struct c2_cm_cp;
struct c2_cm;

/**
 * @todo replace this hard coded size with actual number from confc.
 */
enum {
        C2_CP_SIZE = 4096
};

/**
 * Copy packet priority.
 *
 * Copy packets are assigned a priority (greater numerical value
 * corresponds to higher priority). When multiple copy packets are
 * ready to be processed, higher priority ones have a preference.
 */
enum c2_cm_cp_priority {
	C2_CM_CP_PRIORITY_MIN = 0,
	C2_CM_CP_PRIORITY_MAX = 3,
	C2_CM_CP_PRIORITY_NR
};

/** Copy packet FOM generic phases.*/
enum c2_cm_cp_phase {
	/** Copy packet specific initialisation.*/
	C2_CCP_INIT = C2_FOM_PHASE_INIT,

	/**
	 * Releases resources associated with the packet, finalises members
	 * and free the packet.
	 */
	C2_CCP_FINI = C2_FOM_PHASE_FINISH,

	/** Read and fill up the packet.*/
	C2_CCP_READ,

	/** Write packet data.*/
	C2_CCP_WRITE,

	/** Transform the packet.*/
	C2_CCP_XFORM,

	/** Send packet over network.*/
	C2_CCP_SEND,

	/** Received packet from network.*/
	C2_CCP_RECV,

	C2_CCP_NR
};

/** Generic copy packet structure.*/
struct c2_cm_cp {
	/** Copy packet priority.*/
	enum c2_cm_cp_priority	   c_prio;

	struct c2_fom		   c_fom;

	/** Copy packet operations */
	const struct c2_cm_cp_ops *c_ops;

        /** Aggregation group to which this copy packet belongs.*/
        struct c2_cm_aggr_group   *c_ag;

	/** Buffer representing the copy packet data.*/
	struct c2_bufvec          *c_data;

	/** Set and used in case of network send/recv.*/
	struct c2_rpc_bulk	   c_bulk;

	uint64_t		   c_magix;
};

/**
 * Copy packet operations.
 */
struct c2_cm_cp_ops {
	/**
	 * Changes copy packet phase based on current phase and layout
	 * information. This function should set FOM phase internally and return
	 * @b C2_FSO_WAIT or @b C2_FSO_AGAIN.
	 */
	int      (*co_phase_next) (struct c2_cm_cp *cp);

	/** Specific copy packet invariant.*/
	bool     (*co_invariant) (const struct c2_cm_cp *cp);

	/**
	 * Returns a scalar based on copy packet details, used to select a
	 * request handler home locality for copy packet FOM.
	 */
	uint64_t (*co_home_loc_helper) (const struct c2_cm_cp *cp);

	/** Called when copy packet processing is completed successfully. */
	void     (*co_complete) (struct c2_cm_cp *cp);

	/**
	 * Copy machine type specific copy packet destructor.
	 * This is invoked from c2_cm_cp::c_fom::fo_ops::fo_fini().
	 */
	void     (*co_free)(struct c2_cm_cp *cp);

	/** Size of c2_cm_cp_ops::co_action[]. */
	uint32_t co_action_nr;

	/**
         * Per phase action for copy packet. This function should return
	 * @b C2_FSO_WAIT or @b C2_FSO_AGAIN.
	 */
	int      (*co_action[]) (struct c2_cm_cp *cp);
};

void c2_cm_cp_module_init(void);

/**
 * Initialises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == CCP_INIT
 * @post cp->c_fom.fo_phase == C2_FOPH_INIT
 */
void c2_cm_cp_init(struct c2_cm_cp *cp);

/**
 * Finalises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == C2_FOPH_FINISH
 */
void c2_cm_cp_fini(struct c2_cm_cp *cp);

/** Submits copy packet FOM to request handler for processing.*/
void c2_cm_cp_enqueue(struct c2_cm *cm, struct c2_cm_cp *cp);

bool c2_cm_cp_invariant(const struct c2_cm_cp *cp);

/**
 * Returns the size of the bufvec of the copy packet.
 * Initialized at time of configuration from layout info.
 * It is also used for buffer pool provisioning.
 */
c2_bcount_t c2_cm_cp_data_size(struct c2_cm_cp *cp);

/**
 @}
 */

#endif /*  __COLIBRI_CM_CP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
