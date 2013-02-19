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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 02/22/2012
 */

#pragma once

#ifndef __MERO_CM_CP_H__
#define __MERO_CM_CP_H__

#include "lib/vec.h"
#include "lib/tlist.h"
#include "net/net.h"

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
 *	- @b m0_cm_cp  : Generic copy packet.
 *	- @b m0_cm_ops : Copy packet operations.
 * @section CPDLD-fspec-sub Subroutines
 *	- @b m0_cm_cp_init() : Initialises copy packet members and calls
 *			       c_ops->init() to initialise specific data.
 *	- @b m0_cm_cp_fini() : Finalises copy packet members and calls
 *			       c_ops->fini() internal to finalise specific data.
 *	- @b m0_cm_cp_enqueue() : Posts copy packet FOM for execution.
 *
 * @subsection CPDLD-fspec-sub-acc Accessors and Invariants
 *	- @b m0_cm_cp_invaraint()
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

struct m0_cm_cp;
struct m0_cm;

/**
 * @todo replace this hard coded size with actual number from confc.
 */
enum {
        M0_CP_SIZE = 4096
};

/**
 * Copy packet priority.
 *
 * Copy packets are assigned a priority (greater numerical value
 * corresponds to higher priority). When multiple copy packets are
 * ready to be processed, higher priority ones have a preference.
 */
enum m0_cm_cp_priority {
	M0_CM_CP_PRIORITY_MIN = 0,
	M0_CM_CP_PRIORITY_MAX = 3,
	M0_CM_CP_PRIORITY_NR
};

/** Distinguishes IO operation for a copy packet. */
enum m0_cm_cp_io_op {
	M0_CM_CP_READ,
	M0_CM_CP_WRITE
};

/** Copy packet FOM generic phases. */
enum m0_cm_cp_phase {
	/** Copy packet specific initialisation. */
	M0_CCP_INIT = M0_FOM_PHASE_INIT,

	/**
	 * Releases resources associated with the packet, finalises members
	 * and free the packet.
	 */
	M0_CCP_FINI = M0_FOM_PHASE_FINISH,

	/** Read and fill up the packet. */
	M0_CCP_READ,

	/** Write packet data. */
	M0_CCP_WRITE,

	/** Wait for IO completion. */
	M0_CCP_IO_WAIT,

	/** Transform the packet. */
	M0_CCP_XFORM,

	/** Transform the packet. */
	M0_CCP_XFORM_WAIT,

	/** Send packet over network. */
	M0_CCP_SEND,

	/** Received packet from network. */
	M0_CCP_RECV,

	M0_CCP_NR
};

/** Generic copy packet structure.*/
struct m0_cm_cp {
	/** Copy packet priority.*/
	enum m0_cm_cp_priority	   c_prio;

	struct m0_fom		   c_fom;

	/** Copy packet operations */
	const struct m0_cm_cp_ops *c_ops;

        /** Aggregation group to which this copy packet belongs.*/
        struct m0_cm_aggr_group   *c_ag;

	/** Index of this copy packet in aggregation group. */
	uint64_t                   c_ag_cp_idx;

	/**
	 * Bitmap of the indices of copy packets in an aggregation
	 * group that have been transformed to this resultant copy packet.
	 */
	struct m0_bitmap           c_xform_cp_indices;

	/** List of buffers holding data. */
	struct m0_tl               c_buffers;

	/** Buffer representing the copy packet data.*/
	//struct m0_bufvec          *c_data;

	uint32_t                   c_buf_nr;

	uint32_t                   c_data_seg_nr;

	/** Set and used in case of network send/recv.*/
	struct m0_rpc_bulk	   c_bulk;

	/** Distinguishes IO operation. */
	enum m0_cm_cp_io_op        c_io_op;

	uint64_t		   c_magix;
};

/**
 * Copy packet operations.
 */
struct m0_cm_cp_ops {
	/**
	 * Changes copy packet phase based on current phase and layout
	 * information. This function should set FOM phase internally and return
	 * @b M0_FSO_WAIT or @b M0_FSO_AGAIN.
	 */
	int      (*co_phase_next) (struct m0_cm_cp *cp);

	/** Specific copy packet invariant.*/
	bool     (*co_invariant) (const struct m0_cm_cp *cp);

	/**
	 * Returns a scalar based on copy packet details, used to select a
	 * request handler home locality for copy packet FOM.
	 */
	uint64_t (*co_home_loc_helper) (const struct m0_cm_cp *cp);

	/** Called when copy packet processing is completed successfully. */
	void     (*co_complete) (struct m0_cm_cp *cp);

	/**
	 * Copy machine type specific copy packet destructor.
	 * This is invoked from m0_cm_cp::c_fom::fo_ops::fo_fini().
	 */
	void     (*co_free)(struct m0_cm_cp *cp);

	/** Size of m0_cm_cp_ops::co_action[]. */
	uint32_t co_action_nr;

	/**
         * Per phase action for copy packet. This function should return
	 * @b M0_FSO_WAIT or @b M0_FSO_AGAIN.
	 */
	int      (*co_action[]) (struct m0_cm_cp *cp);
};

M0_INTERNAL void m0_cm_cp_module_init(void);

/**
 * Initialises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == CCP_INIT
 * @post cp->c_fom.fo_phase == M0_FOPH_INIT
 */
M0_INTERNAL void m0_cm_cp_init(struct m0_cm_cp *cp);

M0_INTERNAL void m0_cm_cp_fom_init(struct  m0_cm_cp *cp);

/**
 * Finalises generic copy packet and copy packet FOM.
 *
 * @pre cp->c_fom.fo_phase == M0_FOPH_FINISH
 */
M0_INTERNAL void m0_cm_cp_fini(struct m0_cm_cp *cp);

/** Submits copy packet FOM to request handler for processing.*/
M0_INTERNAL void m0_cm_cp_enqueue(struct m0_cm *cm, struct m0_cm_cp *cp);

M0_INTERNAL bool m0_cm_cp_invariant(const struct m0_cm_cp *cp);

M0_INTERNAL void m0_cm_cp_buf_add(struct m0_cm_cp *cp,
				  struct m0_net_buffer *nb);

M0_INTERNAL void m0_cm_cp_buf_release(struct m0_cm_cp *cp);

M0_TL_DESCR_DECLARE(cp_data_buf, M0_INTERNAL);
M0_TL_DECLARE(cp_data_buf, M0_INTERNAL, struct m0_net_buffer);

/**
 @}
 */

#endif /*  __MERO_CM_CP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
