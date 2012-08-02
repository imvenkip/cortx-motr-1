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

#ifndef __COLIBRI_CM_CP_H__
#define __COLIBRI_CM_CP_H__

#include "lib/mutex.h"
#include "lib/tlist.h"
#include "lib/vec.h"

#include "fop/fom.h"

/**
 * @addtogroup Agents
 * @{
 */

struct c2_cm_cp;
struct c2_cm;

/**
 * Copy packets priority.
 * Copy packets are assigned a priority (greater numerical value
 * corresponds to higher priority). When multiple copy packets are
 * ready to be processed, higher priority ones have a preference.
 */
enum c2_cm_cp_priority {
	C2_CM_CP_PRIORITY_MIN = 0,
	C2_CM_CP_PRIORITY_MAX = 3,
	C2_CM_CP_PRIORITY_NR
};

/** Copy packet states */
enum c2_cm_cp_state {
	C2_CPS_INITIALISED,
	C2_CPS_STORAGE_IN_WAIT,
	C2_CPS_STORAGE_OUT_WAIT,
	C2_CPS_NETWORK_IN_WAIT,
	C2_CPS_NETWORK_OUT_WAIT,
	C2_CPS_COLLECTING_WAIT,
	C2_CPS_FINIALISED
};

/**
 * Copy packet.
 *
 * Copy packet is used for data transfer between various copy machine agents.
 * Copy packet is linked to the aggregation group.
 *
 * Copy packet state diagram:
 *
 * @verbatim
 *
 *     New copy packet             new copy packet
 *            +<-------INITIALISED------->+
 *            |             |             |
 *            |         new |packet	  |
 *            |             |             |
 *    +-WAIT_STORAGE_IN     |	  WAIT_NETWORK_IN-+
 *    |       |             |             |       |
 *    |       |             |             |       |
 *    |       +------------>V<------------+       |
 *    |			WAIT_COLLECT	          |
 *    |       +<------------|------------>+	  |
 *    |       |             |             |	  |
 *    |       V             |             V	  |
 *    +->WAIT_NETWORK_OUT   |	WAIT_STORAGE_OUT<-+
 *            |             |             |
 *            |             |             |
 *	      |             V             |
 *            +--------->FINALISED<-------+
 *
 * @endverbatim
 */
struct c2_cm_cp {
	/** Copy packet priority. @ref c2_cm_cp_priority */
	uint32_t                   c_priority;

	struct c2_fom		   c_fom;

	/** Copy packet operations */
	const struct c2_cm_cp_ops *c_ops;

	/** Buffer representing the copy packet data */
	struct c2_bufvec          *c_data;

        /** Array of starting extent indices. */
        c2_bindex_t               *c_index;

        /** Aggregation group to which this copy packet belongs. */
        struct c2_cm_aggr_group   *c_ag;


	struct c2_cm		  *c_cm;

	uint64_t                   c_magix;

};

/** Copy packet operations */
struct c2_cm_cp_ops {

	/** Allocates copy packet and it's data buffer. */
	int  (*cpo_alloc)    (struct c2_cm *cm, struct c2_cm_cp **cp);

	/**
	 * Releases any resources associated with the packet.
	 * Called when the generic code is about to free a packet.
	 */
	void (*cpo_release)  (struct c2_cm_cp *cp);

	/**
	 * Called when copy packet processing is completed successfully.
	 */
	void (*cpo_complete) (struct c2_cm_cp *packet);
};

/**
 * Initialises copy packet.
 *
 * @post packet->cp_state == CPS_INITIALISED
 */
void c2_cm_cp_init(struct c2_cm_cp *packet);

/**
 * Finalises copy packet.
 *
 * @pre packet->cp_state == CPS_FINIALISED
 */
void c2_cm_cp_fini(struct c2_cm_cp *packet);

/**
 * Enqueues a copy packet into agent
 */
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
