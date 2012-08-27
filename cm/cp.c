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
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 02/22/2012
 */

#include "lib/errno.h"

#include "cm/cp.h"
#include "cm/cm.h"

/**
 * @page CPDLD Copy Packet DLD
 *
 *   - @ref CPDLD-ovw
 *   - @ref CPDLD-def
 *   - @ref CPDLD-req
 *   - @ref CPDLD-depends
 *   - @ref CPDLD-highlights
 *   - @subpage CPDLD-fspec "Functional Specification" <!-- Note @subpage -->
 *   - @ref CPDLD-lspec
 *      - @ref CPDLD-lspec-comps
 *      - @ref CPDLD-lspec-state
 *      - @ref CPDLD-lspec-thread
 *   - @ref CPDLD-conformance
 *   - @ref CPDLD-ut
 *   - @ref CPDLD-st
 *   - @ref CPDLD-ref
 *
 *   <hr>
 *   @section CPDLD-ovw Overview
 *
 *   Copy packet is the data structure used to describe the packet flowing
 *   between various copy machine replica nodes. It is an entity which has data
 *   as well as operation to work. Copy packets are FOMs of special type,
 *   created when a data re-structuring request is posted to replica.
 *
 *   Copy packet processing logic is implemented in non-blocking way. Packet has
 *   buffers to carry data and FOM for execution in context of request handler.
 *   It can perform various kind of work which depends on the it's stage
 *   (i.e. FOM phase) in execution.
 *
 *   <hr>
 *   @section CPDLD-def Definitions
 *   - <b>Copy Packet:</b> A chunk of input data traversing through the copy
 *   machine.
 *
 *   - <b>Copy packet acknowledgement:</b> Reply received, representing
 *   successful processing of the copy packet. With this acknowledgement, copy
 *   packet release various resources, update its internal state.
 *
 *   - <b>Next phase function:</b> Given a copy packet, this identifies the
 *   phase that has to be assigned to this copy packet. The next phase function
 *   determines the routing and execution of copy packets through the copy
 *   machine.
 *
 *   <hr>
 *   @section CPDLD-req Requirements
 *
 *   - @b r.cm.cp Copy packet abstraction implemented such that it
 *	  represents the data to be transferred within replica.
 *
 *   - @b r.cm.cp.async Every read-write (receive-send) by replica
 *	  should follow non-blocking processing model of Colibri design.
 *
 *   - @b r.cm.buffer_pool Copy machine should provide a buffer pool, which
 *	  is efficiently used for copy packet data.
 *
 *   - @b r.cm.cp.bulk_transfer All data packets (except control packets)
 *	  that are sent over RPC should use bulk-interface for communication.
 *
 *   - @b r.cm.addb Copy packet should have its own addb context, (similar
 *	  to fop), although it uses various different addb locations, this will
 *	  trace the entire path of the copy packet.
 *
 *   <hr>
 *   @section CPDLD-depends Dependencies
 *
 *   - @b r.cm.service Copy packets FOMs are executed in context of copy
 *	  machine replica service.
 *
 *   - @b r.cm.ops Replica provides operations to create, configure and
 *	  execute copy packet FOMs.
 *
 *   - @b r.layout Data restructuring needs layout info.
 *
 *   - @b r.layout.input-iterator Iterate over layout info to create packets
 *	  and forward it in replica.
 *
 *   - @b r.resource Resources like buffers, CPU cycles, network bandwidth,
 *	  storage bandwidth are needed by copy packet FOM during execution.
 *
 *   - @b r.confc Data from configuration will be used to initialise copy
 *	  packets.
 *
 *   <hr>
 *   @section CPDLD-highlights Design Highlights
 *
 *   - Copy packet is implemented as FOM, which inherently has non-blocking
 *     model of colibri.
 *
 *   - Distributed sliding window algorithm is used to process copy packets
 *     within copy machine replica.
 *
 *   - Layout is updated periodically as the restructuring progresses.
 *
 *   <hr>
 *   @section CPDLD-lspec Logical Specification
 *
 *   - @ref CPDLD-lspec-comps
 *      - @ref DLDCPInternal  <!-- Note link -->
 *   - @ref CPDLD-lspec-state
 *   - @ref CPDLD-lspec-thread
 *
 *   @subsection CPDLD-lspec-comps Component Overview
 *
 *   <b>Copy packet functionality is split into two parts:</b>
 *
 *	- generic functionality, implemented by cm/cp.[hc] and
 *
 *      - copy packet type functionality which based on copy machine type.
 *        (e.g. SNS, Replication, &c).
 *
 *   <b>Copy packet creation:</b>
 *   Given the size of the buffer pool, the replica calculates its initial
 *   sliding window (see c2_cm_sw) size. Once the replica learns window sizes
 *   of every other replica, it can produce copy packets that replicas
 *   (including this one) are ready to process.
 *
 *      - start, device failure triggers copy machine data re-structuring
 *        and it should make sure that sliding windows has enough packets
 *        for processing by creating them at start of operation.
 *
 *      - has space, after completion of each copy packet, space in sliding
 *        window is checked. If space exists, then copy packets will be created.
 *
 *   <b>Copy Packet destruction:</b>
 *   Copy packet is destroyed by setting it's phase to FINI. Following are some
 *   cases where copy packet is finalised.
 *
 *	- On notification of copy packet data written to device/container.
 *
 *	- During transformation, unwanted packets are finalised.
 *
 *	- On completion of copy packet transfer over network.
 *
 *   <b>Copy packet cooperation within replica:</b>
 *   Copy packet needs resources (memory, processor, &c.) to do processing:
 *
 *	- Needs buffers to keep data during IO;
 *
 *	- Needs buffers to keep data until the transfer is finished;
 *
 *	- Needs buffers to keep intermediate checksum until all units of an
 *	  aggregation group have been received.
 *
 *   The same copy packet (and its associated buffers) will go through various
 *   stages. Data read from device creates a copy packet, then copy packet
 *   transitions to data transformation phase, which, after reconstructing the
 *   data, transitions to data write or send, which submits IO. On IO
 *   completion, the copy packet is destroyed.
 *
 *   At the re-structuring start time, replica is given a certain (configurable)
 *   amount of memory. This memory is allocated by copy machine buffer pool and
 *   used for copy packet data buffers. Buffers get provisioned when trigger
 *   happens.
 *
 *   Given the size of the buffer pool, the replica calculates its initial
 *   sliding window size.
 *
 *   Once the replica learns window sizes of every other replica, it can
 *   activates copy packet FOMs. Copy machine replica submits copy packets FOMs
 *   to request handler.
 *
 *   @subsection CPDLD-lspec-state State Specification
 *
 *   <b>Copy packet is a state machine, goes through following stages:</b>
 *
 *   - @b INIT   Copy packet gets initialised with input data. e.g In SNS,
 *		 extent, COB, &c gets initialised. Usually this will be done
 *		 with some iterator over layout info.
 *
 *   - @b READ   Reads data from its associated container or device according
 *		 to the input information, and places the data in a copy packet
 *		 data buffer. Before doing this, it needs to grab necessary
 *		 resources: memory, locks, permissions, CPU/disk bandwidth,
 *		 etc. Data/parity is encapsulated in copy packet, and the copy
 *		 packets are transfered to next phase.
 *
 *   - @b WRITE  Writes data from copy packet data buffer to the container or
 *		 device. Spare container and offset to write is identified from
 *		 layout information.
 *
 *   - @b XFORM  Data restructuring is done in this phase. This phase would
 *		 typically process a lot of local copy packets. E.g., for SNS
 *		 repair machine, a file typically has a component object (cob)
 *		 on each device in the pool, which means that a node could
 *		 (and should) calculate "partial parity" of all local units,
 *		 instead of sending each of them separately across the network
 *		 to a remote copy machine replica.
 *
 *   - @b SEND   Send copy packet over network. Control FOP and bulk transfer
 *		 are used for sending copy packet.
 *
 *   - @b RECV   Copy packet data is received from network. On receipt of
 *		 control FOP, copy packet is created and FOM is submitted for
 *		 execution and phase is set RECV, which will eventually receive
 *		 data using rpc bulk.
 *
 *   - @b FINI  Finalises copy packet.
 *
 *   Specific copy packet can have states/phases in addition to these phases.
 *   Additional states may be used to do processing under one of above phases.
 *   Handling of additional stages/states is done by specific code.
 *
 *   Transition of standard phases is done by next phase function. It will
 *   produce the next phase according to the configuration of the copy machine
 *   and the copy packet itself.
 *
 *   <b>State diagram for copy packet:</b>
 *   @dot
 *   digraph {
 *	subgraph A1 {
 *	   start [shape=Mdiamond];
 *	   end [shape=doublecircle];
 *	}
 *	subgraph A2 {
 *	   size = "4,4"
 *	   node [shape=ellipse, fontsize=12]
 *	   start -> INIT
 *	   INIT  -> READ -> SEND -> FINI
 *	   INIT  -> RECV -> WRITE -> FINI
 *	   INIT  -> XFORM -> FINI
 *	   READ  -> XFORM -> SEND
 *	   RECV  -> XFORM -> WRITE
 *	   FINI  -> end
 *	}
 *   }
 *   @enddot
 *
 *   @subsection CPDLD-lspec-thread Threading and Concurrency Model
 *
 *   @dot
 *	digraph {
 *	  node [shape=plaintext, fontsize=12]
 *	  subgraph cluster_m1 { // represents mutex scope
 *	  // sorted R-L so put mutex name last to align on the left
 *	  rank = same;
 *	  n1_2 [label="c2_cm_cp_init()"];  // procedure using mutex
 *	  n1_1 [label="c2_cm_cp_fini()"];
 *	  n1_0 [label="c2_cm:c_fom:fo_loc:fl_group:s_lock"];// mutex name
 *	}
 *
 *	subgraph cluster_m2 {
 *	  rank = same;
 *	  n2_3 [label="phase()"];
 *	  n2_2 [label="complete()"];
 *	  n2_1 [label="action()"];
 *	  n2_0 [label="c2_cm:c_fom:fo_loc:fl_group:s_lock"];
 *	}
 *
 *	label="Mutex usage and locking order in the copy packet";
 *	n1_0 -> n2_0;  // locking order
 *   }
 *   @enddot
 *
 *   <hr>
 *   @section CPDLD-conformance Conformance
 *
 *   - @b i.cm.cp Replicas communicate using copy packet structure.
 *
 *   - @b i.cm.cp.async Copy packet are implemented as FOM. FOM in request
 *	  handler infrastructure makes it non-blocking.
 *
 *   - @b i.cm.buffer_pool Buffer pools are managed by copy machine which
 *	  cater to the requirements of copy packet data.
 *
 *   - @b i.cm.cp.bulk_transfer All data packets (except control packets)
 *	  that are sent over RPC, use bulk-interface for communication.
 *
 *   - @b i.cm.cp.addb copy packet uses ADDB context of copy machine.
 *
 *   <hr>
 *   @section CPDLD-ut Unit Tests
 *   - Basic Test: Alloc, Init, fini and free
 *
 *   <hr>
 *   @section CPDLD-st System Tests
 *
 *   <hr>
 *   @section CPDLD-ref References
 *
 *   - <a href="https://docs.google.com/a/xyratex.com/document/d/1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#">
 *   HLD of SNS Repair</a>
 *
 *   - <a href="https://docs.google.com/a/xyratex.com/document/d/1ZlkjayQoXVm-prMxTkzxb1XncB6HU19I19kwrV-8eQc/edit#">
 *   HLD of Copy machine and agents</a>
 *
 */

/**
 * @defgroup DLDCPInternal Copy packet internal
 * @ingroup CP
 *
 * @see @ref CPDLD and @ref CPDLD-lspec
 *
 * @{
 */

static const struct c2_fom_type_ops cp_fom_type_ops = {
        .fto_create = NULL
};

static struct c2_fom_type cp_fom_type = {
        .ft_ops = &cp_fom_type_ops
};

static void cp_fom_fini(struct c2_fom *fom)
{
        struct c2_cm_cp *cp;

        cp = container_of(fom, struct c2_cm_cp, c_fom);
	/*
         * @todo Before freeing copy packet check this is last packet
         * from aggregation group, if yes then free aggregation group along
         * with copy packet.
         */
	c2_cm_cp_fini(cp);
	/*todo It will check for has_space if yes call packet creating logic.*/
}

static size_t cp_fom_locality(const struct c2_fom *fom)
{
        struct c2_cm_cp *cp;

        cp = container_of(fom, struct c2_cm_cp, c_fom);
        C2_PRE(c2_cm_cp_invariant(cp));

        return 0;
}

static int cp_fom_state(struct c2_fom *fom)
{
        struct c2_cm_cp *cp = container_of(fom, struct c2_cm_cp, c_fom);

        C2_PRE(c2_cm_cp_invariant(cp));
	return cp->c_ops->co_action[c2_fom_phase(fom)](cp);
}

/** Copy packet FOM operations */
static const struct c2_fom_ops cp_fom_ops = {
        .fo_fini          = cp_fom_fini,
        .fo_state         = cp_fom_state,
        .fo_home_locality = cp_fom_locality
};

/** @} end internal */

/**
   @addtogroup CP
   @{
 */

bool c2_cm_cp_invariant(struct c2_cm_cp *cp)
{
	int phase = cp->c_fom.fo_phase;

	return cp->c_ops != NULL && cp->c_data != NULL &&
	       (phase == C2_FOM_PHASE_INIT || (phase >= C2_CCP_INIT &&
					       phase <= C2_CCP_FINI)) &&
	       ergo(phase > C2_CCP_INIT && phase <= C2_CCP_FINI,
		    c2_stob_id_is_set(&cp->c_id) && cp->c_ag != NULL);
}

void c2_cm_cp_init(struct c2_cm_cp *cp, const struct c2_cm_cp_ops *ops,
		   struct c2_bufvec *buf)
{
	C2_PRE(cp != NULL && ops != NULL && buf != NULL);

	cp->c_ops = ops;
	cp->c_data = buf;
	c2_fom_init(&cp->c_fom, &cp_fom_type, &cp_fom_ops, NULL, NULL);

	C2_POST(c2_cm_cp_invariant(cp));
}

void c2_cm_cp_fini(struct c2_cm_cp *cp)
{
	c2_fom_fini(&cp->c_fom);
}

void c2_cm_cp_enqueue(struct c2_cm *cm, struct c2_cm_cp *cp)
{
}

int c2_cm_cp_create(struct c2_cm *cm)
{
	struct c2_cm_cp *cp;
	struct c2_cm_sw *sw = &cm->cm_sw;

	while (sw->sw_ops->swo_has_space(sw)) {
	       cp = cm->cm_ops->cmo_cp_alloc(cm);
	       if (cp == NULL)
		   return -ENOENT;
	       c2_cm_cp_enqueue(cm, cp);
        }

	return 0;
}

/** @} end-of-CPDLD */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
