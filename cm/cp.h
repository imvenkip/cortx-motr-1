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
	C2_CCP_FINI,

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

	C2_CCP_PHASE_NR
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

        /** Array of starting extent indices. */
        c2_bindex_t               *c_index;

	/** Buffer representing the copy packet data */
	struct c2_bufvec          *c_data;

	/** Set and used in case of network send/recv.*/
	struct c2_rpc_bulk	   c_bulk;

	uint64_t		   c_magix;
};

/**
 * Copy packet operations.
 *
 * A copy machine has a handler which handles FOP requests. A copy machine is
 * responsible to create corresponding copy packet FOMs to do the actual work.
 */
struct c2_cm_cp_ops {
	/** Per phase action for copy packet */
	int  (*co_action[C2_CCP_PHASE_NR]) (struct c2_cm_cp *cp);

	/** Called when copy packet processing is completed successfully.*/
	void (*co_complete) (struct c2_cm_cp *cp);

	/**
	 * Changes copy packet phase based on current phase and layout
	 * information. This function should set FOM phase internally and return
	 * @b C2_FSO_WAIT or @b C2_FSO_AGAIN.
	 */
	int  (*co_phase) (struct c2_cm_cp *cp);

	/** Specific copy packet invariant.*/
	bool (*co_invariant) (const struct c2_cm_cp *cp);
};

/**
 * Initialises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == CCP_INIT
 * @post cp->c_fom.fo_phase == C2_FOPH_INIT
 */
void c2_cm_cp_init(struct c2_cm_cp *cp, const struct c2_cm_cp_ops *ops,
		   struct c2_bufvec *buf);

/**
 * Finalises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == C2_FOPH_FINISH
 */
void c2_cm_cp_fini(struct c2_cm_cp *cp);

/** Submits copy packet FOM to request handler for processing.*/
void c2_cm_cp_enqueue(struct c2_cm *cm, struct c2_cm_cp *cp);

bool c2_cm_cp_invariant(struct c2_cm_cp *cp);

int c2_cm_cp_create(struct c2_cm *cm);

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
