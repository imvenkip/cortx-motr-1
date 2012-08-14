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

#include "lib/mutex.h"
#include "lib/tlist.h"
#include "lib/vec.h"

#include "fop/fom.h"

/**
 * @page CPDLD-fspec Copy Packet Functional Specification
 *
 * - @ref CPDLD-fspec-ds
 * - @ref CPDLD-fspec-sub
 * - @ref cp "Copy Packet Functional Specification" <!-- Note link -->
 *
 * @section CPDLD-fspec-ds Data Structures
 *	- c2_cm_cp  : Generic copy packet.
 *	- c2_cm_ops : Copy packet operations.
 * @section CPDLD-fspec-sub Subroutines
 *	- c2_cm_cp_init()    : Initialises copy packet members and calls
 *			       c_ops->init() to initialise specific data.
 *	- c2_cm_cp_fini()    : Finalises copy packet members and calls
 *			       c_ops->fini() internal to finalise specific data.
 *	- c2_cm_cp_enqueue() : Post copy packet FOM for execution.
 *
 * @subsection CPDLD-fspec-sub-acc Accessors and Invariants
 *	- c2_cm_cp_invaraint()
 *
 * @subsection CPDLD-fspec-sub-opi Operational Interfaces
 *	- cp_fom_fini()
 *	- cp_fom_locality()
 *	- cp_fom_state()
 *
 *	@see @ref cp
 */

/**
 * @defgroup CP Copy Packet
 * @ingroup CM
 *
 * @see The @ref cp "Copy packet" its
 * @ref CPDLD-fspec "Copy Packet Functional Specification"
 *
 * @{
 */

struct c2_cm_cp;
struct c2_cm;

/**
 * Copy packets priority.
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

/**
 * Copy packet FOM generic phases.
 *
 * Packet's FOM doesn't use standard phases, but we don't want to step on
 * C2_FOPH_FINISH which has special meaning in fom.c.
 */
enum c2_cm_cp_phase {
	/** Phase specific initialisation.*/
	CCP_INIT = C2_FOPH_NR + 1,

	/** Read and fill up the packet.*/
	CCP_READ,

	/** Write packet packet data.*/
	CCP_WRITE,

	/** Packet is to be transformed.*/
	CCP_XFORM,

	/** Send packet over network.*/
	CCP_SEND,

	/** Received packet from network.*/
	CCP_RECV,

	/** Finalisation of packet.*/
	CCP_FINI
};

/** Generic copy packet structure.*/
struct c2_cm_cp {
	/** Copy packet priority.*/
	enum c2_cm_cp_priority	   c_prio;

	struct c2_fom		   c_fom;

	/** Copy packet operations */
	const struct c2_cm_cp_ops *c_ops;

	/** Buffer representing the copy packet data */
	struct c2_bufvec          *c_data;

        /** Array of starting extent indices. */
        c2_bindex_t               *c_index;

        /** Aggregation group to which this copy packet belongs.*/
        struct c2_cm_aggr_group   *c_ag;

	/** Set and used in case of read/write.*/
	struct c2_stob_id	   c_id;

	/** Set and used in case of network send/recv.*/
	struct c2_rpc_bulk	  *c_bulk;
};

/**
 * Copy packet operations
 *
 * A copy machine has a handler which handles FOP requests. A copy machine is
 * responsible to create corresponding copy packet FOMs to do the actual work.
 */
struct c2_cm_cp_ops {
	/** Initialise specific copy packet members.*/
	int  (*co_init)     (struct c2_cm_cp *cp);

	/** Fill up the copy packet data.*/
	int  (*co_read)	    (struct c2_cm_cp *cp);

	/** Write copy packet data.*/
	int  (*co_write)    (struct c2_cm_cp *cp);

	/** Send copy packet over network.*/
	int  (*co_send)	    (struct c2_cm_cp *cp);

	/** Receive and forward copy packet.*/
	int  (*co_recv)	    (struct c2_cm_cp *cp);

	/** Transform copy packet.*/
	int  (*co_xform)    (struct c2_cm_cp *cp);

	/** Called when copy packet processing is completed successfully.*/
	void (*co_complete) (struct c2_cm_cp *cp);

	/**
	 * Changes copy packet phase based on current phase and layout
	 * information.
	 */
	int  (*co_phase)    (struct c2_cm_cp *cp);

	/**
	 * Releases resources associated with the packet, finalises members
	 * and free the packet.
	 */
	void (*co_fini)	    (struct c2_cm_cp *cp);
};

/**
 * Initialises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == CCP_INIT
 * @post cp->c_fom.fo_phase == C2_FOPH_INIT
 */
void c2_cm_cp_init(struct c2_cm_cp *cp, struct c2_cm_aggr_group *ag,
		   const struct c2_cm_cp_ops *ops, struct c2_bufvec *buf);

/**
 * Finalises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == C2_FOPH_FINISH
 */
void c2_cm_cp_fini(struct c2_cm_cp *cp);

/** Submit copy packet for processing.*/
void c2_cm_cp_enqueue(struct c2_cm *cm, struct c2_cm_cp *cp);

bool c2_cm_cp_invariant(struct c2_cm_cp *cp);

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
