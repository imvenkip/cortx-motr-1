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
 * @page DLD-cp-fspec Copy Packet Functional Specification
 *
 * - @ref DLD-cp-fspec-ds
 * - @ref DLD-cp-fspec-sub
 * - @ref cp "Copy Packet Functional Specification" <!-- Note link -->
 *
 * @section DLD-cp-fspec-ds Data Structures
 *	- c2_cm_cp  : Generic copy packet.
 *	- c2_cm_ops : Copy packet operations.
 * @section DLD-cp-fspec-sub Subroutines
 *	- c2_cm_cp_init()    : Initialises copy packet members and calls
 *			       c_ops->init() to initialise specific data.
 *	- c2_cm_cp_fini()    : Finalises copy packet members and calls
 *			       c_ops->fini() internal to finalise specific data.
 *	- c2_cm_cp_enqueue() : Post copy packet FOM for execution.
 *
 * @subsection DLD-cp-fspec-sub-acc Accessors and Invariants
 *	- c2_cm_cp_invaraint()
 *
 * @subsection DLD-fspec-sub-opi Operational Interfaces
 *	- cp_fom_fini()
 *	- cp_fom_locality()
 *	- cp_fom_state()
 *
 *	@see @ref cp
 */

/**
 * @defgroup cp Copy Packet
 *
 * When an instance of a copy machine type is created, a data structure copy
 * machine replica is created on each node (technically, in each request
 * handler) that might participate in the re-structuring.
 *
 * Copy packets are FOM of special type, created when a data re-structuring
 * request is posted to replica. Copy packet processing logic is implemented in
 * non-blocking way.
 *
 * Copy packet functionality split into two parts:
 *
 *	- generic functionality, implemented by cm/cp.[hc] directory and
 *
 *	- copy packet type functionality which based on copy machine type.
 *	  (e.g. SNS, Replication, &c).
 *
 * Copy packet creation:
 *
 *  Given the size of the buffer pool, the replica calculates its initial
 *  sliding window (@see c2_cm_sw) size. Once the replica learns window
 *  sizes of every other replica, it can produce copy packets that replicas
 *  (including this one) are ready to process.
 *
 *	- start, device failure triggers copy machine data re-structuring
 *	  and it should make sure that sliding windows has enough packets
 *	  for processing by creating them at start of operation.
 *
 *	- has space, after completion of each copy packet, space in sliding
 *	  window checked. Copy packet exists then copy packets will be created.
 *
 *
 * Copy machine IO:
 *
 * Transformation:
 *
 * Cooperation within replica:
 *
 * Resource:
 *	- Buffer pool
 *	- Storage BW
 *	- Extent Locks
 *	- Network bandwidth
 *	- CPU cycles
 *
 * @see The @ref cp "Copy packet" its
 * @ref DLD-cp-fspec "Copy Packet Functional Specification"
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

/**
 * Copy packet.
 *
 * Copy packet is the data structure used to describe the packet flowing between
 * various copy machine replica nodes. It is entity which has data as well as
 * operation to work. Copy packet has buffers to carry data and FOM for
 * execution in context of request handler. It can perform various kind of work
 * which depend on the it's stage (i.e. FOM phase) in execution. Phase_next()
 * responsible for stage change of copy packet. It is a state machine, goes
 * through following stages:
 *
 *	- READ
 *	- WRITE
 *	- XFORM
 *	- SEND
 *	- RECV
 *	- Non-std: Copy packet FOM can have phases addition these phases.
 *		   Additional phases will be used to do processing under one of
 *		   above phases.
 *
 * Trasition of standard phases is done by phase_next().
 *
 * @todo c2_cm_cp:c_fom:fo_loc used for transformation (e.g XOR).
 * @todo has_space in sliding window.
 *
 * Copy packet state diagram:
 *
 * @verbatim
 *
 *     New copy packet             new copy packet
 *          +<---------INIT-------->+
 *          |	        |	    |
 *          |           |           |
 *    +----READ     new |packet	   RECV----+
 *    |     |           |           |      |
 *    |     +---------->V<----------+      |
 *    |		      XFORM	           |
 *    |     +<----------|---------->+	   |
 *    |     |           |           |	   |
 *    |     V           |           V	   |
 *    +--->SEND	        |	  WRITE<---+
 *	    |           V           |
 *          +--------->FINI<--------+
 *
 * @endverbatim
 */
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

	struct c2_cm		  *c_cm;
};

/**
 * Copy packet operations
 *
 * A copy machine has a handler which handles FOP requests. A copy machine is
 * responsible to create corresponding copy packet FOMs to do the actual work.
 */
struct c2_cm_cp_ops {

	/** Allocate, initialise copy packet structure.*/
	int  (*co_alloc)    (struct c2_cm *cm, struct c2_cm_cp **cp);

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

	/** Non standard phases handled in this function.*/
	int  (*co_state)    (struct c2_cm_cp *cp);

	/** Called when copy packet processing is completed successfully.*/
	void (*co_complete) (struct c2_cm_cp *cp);

	/**
	 * Releases resources associated with the packet, finalises members
	 * and free the packet.
	 */
	void (*co_free)	    (struct c2_cm_cp *cp);
};

/**
 * Initialises generic copy packet.
 *
 * @pre cp->c_fom.fo_phase == CCP_INIT
 * @post cp->c_fom.fo_phase == C2_FOPH_INIT
 */
void c2_cm_cp_init(struct c2_cm *cm, struct c2_cm_cp *cp,
		   const struct c2_cm_cp_ops *ops);

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
