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

#include "lib/bob.h"    /* c2_bob */
#include "lib/misc.h"   /* C2_IN */
#include "lib/errno.h"  /* ENOMEM */
#include "lib/memory.h" /* C2_ALLOC_PTR */

#include "cm/cp.h"
#include "cm/cm.h"

/**
 * @page DLD-cp DLD Copy Packet
 *
 *   - @ref DLD-cp-ovw
 *   - @ref DLD-cp-def
 *   - @ref DLD-cp-req
 *   - @ref DLD-cp-depends
 *   - @ref DLD-cp-highlights
 *   - @subpage DLD-cp-fspec "Functional Specification" <!-- Note @subpage -->
 *   - @ref DLD-cp-lspec
 *      - @ref DLD-cp-lspec-comps
 *      - @ref DLD-cp-lspec-sc1
 *      - @ref DLD-cp-lspec-state
 *      - @ref DLD-cp-lspec-thread
 *      - @ref DLD-cp-lspec-numa
 *   - @ref DLD-cp-conformance
 *   - @ref DLD-cp-ut
 *   - @ref DLD-cp-st
 *   - @ref DLD-cp-O
 *   - @ref DLD-cp-ref
 *   - @ref DLD-cp-impl-plan
 *
 *   <hr>
 *   @section DLD-cp-ovw Overview
 *
 *   When an instance of a copy machine type is created, a data structure copy
 *   machine replica is created on each node (technically, in each request
 *   handler) that might participate in the re-structuring.
 *
 *   Copy packet is the data structure used to describe the packet flowing
 *   between various copy machine replica nodes. It is entity which has data as
 *   well as operation to work. Copy packets are FOM of special type, created
 *   when a data re-structuring request is posted to replica.
 *
 *   Copy packet processing logic is implemented in non-blocking way. Packet has
 *   buffers to carry data and FOM for execution in context of request handler.
 *   It can perform various kind of work which depend on the it's stage
 *   (i.e. FOM phase) in execution. Phase_next() responsible for stage change
 *   of copy packet.
 *
 *   Copy packet functionality split into two parts:
 *
 *	- generic functionality, implemented by cm/cp.[hc] directory and
 *
 *      - copy packet type functionality which based on copy machine type.
 *        (e.g. SNS, Replication, &c).
 *
 *   Copy packet creation:
 *
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
 *        window checked. Copy packet exists then copy packets will be created.
 *
 *
 *   Copy machine IO:
 *
 *   Transformation:
 *
 *   Cooperation within replica:
 *
 *   Resource:
 *      - Buffer pool
 *      - Storage BW
 *      - Extent Locks
 *      - Network bandwidth
 *      - CPU cycles
 *
 *   @todo c2_cm_cp:c_fom:fo_loc used for transformation (e.g XOR).
 *   @todo has_space in sliding window.
 *
 *   <hr>
 *   @section DLD-cp-def Definitions
 *
 *   <hr>
 *   @section DLD-cp-req Requirements
 *
 *   - <b>R.cm.cp</b> Copy-packet abstraction should be implemented such that it
 *     represents the data to be transferred within replica.
 *
 *   - <b>R.cm.cp.FOM</b> Copy packets should be implemented as FOM.
 *
 *   - <b>R.cm.cp.async</b> Every read-write (receive-send) by replica
 *     should follow non-blocking processing model of Colibri design.
 *
 *   - <b>R.cm.buffer_pool</b> Copy machine should provide a buffer pool, which
 *     is efficiently used for copy packet data.
 *
 *   - <b>R.cm.cp.bulk_transfer</b> All data packets (except control packets)
 *     that are sent over RPC should use bulk-interface for communication.
 *
 *   - <b>R.cm.addb</b> copy packet will use ADDB context of copy machine
 *     replica. Each copy packet might have its own ADDB context as well.
 *
 *   <hr>
 *   @section DLD-cp-depends Dependencies
 *
 *   - <b>R.cm.service</b> Copy packets FOM are executed in context of copy
 *     machine replica service.
 *
 *   - <b>R.cm.ops</b> Replica provides operation to create, configure and
 *     execute copy packet FOMs.
 *
 *   - <b>R.layout</b> Data re-structure needs layout info.
 *
 *   - <b>R.layout.input-set</b> Iterate over layout info to create packets.
 *
 *   - <b>R.layout.output-set</b> Iterate over layout info to forward and write
 *     copy packets.
 *
 *   - <b>R.resource</b> Resources like buffers, CPU cycles, network bandwidth,
 *     storage bandwidth need by copy packet FOM during execution.
 *
 *   - <b>R.confc</b> Data from configuration will be used to initialise copy
 *     packets.
 *
 *   <hr>
 *   @section DLD-cp-highlights Design Highlights
 *
 *   - Copy packet are implemented as FOM, which inherently has non-blocking
 *     model of colibri.
 *
 *   - Distributed sliding window algorithm is used to copy packet processing
 *     within copy machine replica.
 *
 *   - Layout is updated periodically as the re-structuring progresses.
 *
 *   - Copy machine will adjust its operation when it encounters any changes
 *     in the input and output set.
 *
 *   <hr>
 *   @section DLD-cp-lspec Logical Specification
 *
 *   - @ref DLD-cp-lspec-comps
 *   - @ref DLD-cp-lspec-sc1
 *      - @ref DLD-cp-lspec-ds1
 *      - @ref DLD-cp-lspec-sub1
 *      - @ref DLDCPInternal  <!-- Note link -->
 *   - @ref DLD-cp-lspec-state
 *   - @ref DLD-cp-lspec-thread
 *   - @ref DLD-cp-lspec-numa
 *
 *   @subsection DLD-cp-lspec-comps Component Overview
 *
 *   @subsection DLD-cp-lspec-sc1 Subcomponent design
 *
 *   @subsubsection DLD-cp-lspec-ds1 Subcomponent Data Structures
 *
 *   @subsubsection DLD-cp-lspec-sub1 Subcomponent Subroutines
 *
 *   @subsection DLD-cp-lspec-state State Specification
 *
 *   Copy packet is a state machine, goes through following stages:
 *
 *	- INIT
 *      - READ
 *      - WRITE
 *      - XFORM
 *      - SEND
 *      - RECV
 *      - FINI
 *      - Non-std: Copy packet FOM can have phases addition these phases.
 *                 Additional phases will be used to do processing under one of
 *                 above phases.
 *
 *   Transition of standard phases is done by phase().
 *
 *   State diagram for copy packet phases:
 *   @verbatim
 *
 *        New copy packet             new copy packet
 *             +<---------INIT-------->+
 *             |           |           |
 *             |           |           |
 *       +----READ     new |packet    RECV----+
 *       |     |           |           |      |
 *       |     +---------->V<----------+      |
 *       |               XFORM                |
 *       |     +<----------|---------->+      |
 *       |     |           |           |      |
 *       |     V           |           V      |
 *       +--->SEND         |         WRITE<---+
 *             |           V           |
 *             +--------->FINI<--------+
 *
 *   @endverbatim
 *
 *   @subsection DLD-cp-lspec-thread Threading and Concurrency Model
 *
 *   @dot
 *	digraph {
 *	  node [shape=plaintext, style=filled, color=lightgray, fontsize=15]
 *	  subgraph cluster_m1 { // represents mutex scope
 *	  // sorted R-L so put mutex name last to align on the left
 *	  rank = same;
 *	  n1_2 [label="c2_cm_cp_init()"];  // procedure using mutex
 *	  n1_1 [label="c2_cm_cp_fini"];
 *	  n1_0 [label="c2_cm:c_fom:fo_loc:fl_group:s_lock"];// mutex name
 *	}
 *
 *	subgraph cluster_m2 {
 *	  rank = same;
 *	  n2_9 [label="phase()"];
 *	  n2_8 [label="init()"];
 *	  n2_7 [label="fini()"];
 *	  n2_6 [label="read()"];
 *	  n2_5 [label="write()"];
 *	  n2_4 [label="xform()"];
 *	  n2_3 [label="send()"];
 *	  n2_2 [label="recv()"];
 *	  n2_1 [label="state()"];
 *	  n2_0 [label="c2_cm:c_fom:fo_loc:fl_group:s_lock"];
 *	}
 *
 *	label="Mutex usage and locking order in the Copy Machine Agents";
 *	n1_0 -> n2_0;  // locking order
 *   }
 *   @enddot
 *
 *   @subsection DLD-cp-lspec-numa NUMA optimizations
 *
 *   <hr>
 *   @section DLD-cp-conformance Conformance
 *
 *   - <b>I.cm.cp</b> Replicas communicate using copy packet structure.
 *
 *   - <b>I.cm.cp.FOM</b> Copy packets should be implemented as FOM.
 *
 *   - <b>I.cm.cp.async</b> Copy packet FOM in request handler infrastructure
 *     makes it non-blocking.
 *
 *   - <b>I.cm.buffer_pool</b> Buffer pools are managed by copy machine which
 *     cater to the requirements of copy packet data.
 *
 *   - <b>I.cm.cp.bulk_transfer</b> All data packets (except control packets)
 *     that are sent over RPC, use bulk-interface for communication.
 *
 *   - <b>I.cm.cp.addb</b> copy packet uses ADDB context of copy machine.
 *
 *   <hr>
 *   @section DLD-cp-ut Unit Tests
 *   - Basic Test: Alloc, Init, fini and free
 *
 *   <hr>
 *   @section DLD-cp-st System Tests
 *
 *   <hr>
 *   @section DLD-cp-O Analysis
 *
 *   <hr>
 *   @section DLD-cp-ref References
 *
 *   <hr>
 *   @section DLD-cp-impl-plan Implementation Plan
 *
 */

/**
 * @defgroup DLDCPInternal Copy packet internal
 *
 * @see @ref DLD-cp and @ref DLD-cp-lspec
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
        struct c2_cm_cp *cp;

        cp = container_of(fom, struct c2_cm_cp, c_fom);
        C2_ASSERT(c2_cm_cp_invariant(cp));

	switch (fom->fo_phase) {
	case CCP_INIT:
		return cp->c_ops->co_init(cp);
	case CCP_READ:
		return cp->c_ops->co_read(cp);
	case CCP_WRITE:
		return cp->c_ops->co_write(cp);
	case CCP_XFORM:
		return cp->c_ops->co_xform(cp);
	case CCP_SEND:
		return cp->c_ops->co_send(cp);
	case CCP_RECV:
		return cp->c_ops->co_recv(cp);
	case CCP_FINI:
		fom->fo_phase = C2_FOPH_FINISH;
		return C2_FSO_WAIT;
	default:
		C2_IMPOSSIBLE("IMPOSSIBLE");
	}
}

/** Copy packet FOM operations */
static const struct c2_fom_ops cp_fom_ops = {
        .fo_fini          = cp_fom_fini,
        .fo_state         = cp_fom_state,
        .fo_home_locality = cp_fom_locality
};

/** @} end internal */

/**
   @addtogroup cp
   @{
 */

bool c2_cm_cp_invariant(struct c2_cm_cp *cp)
{
	uint32_t phase = cp->c_fom.fo_phase;

	return phase >= CCP_INIT && phase <= CCP_FINI &&
	       c2_fom_invariant(&cp->c_fom) && cp->c_ops != NULL &&
	       cp->c_data != NULL && cp->c_ag != NULL;
}

void c2_cm_cp_init(struct c2_cm_cp *cp, struct c2_cm_aggr_group *ag,
		   const struct c2_cm_cp_ops *ops, struct c2_bufvec *buf)
{
	C2_PRE(cp != NULL && ag != NULL && ops != NULL && buf != NULL);

	cp->c_ag = ag;
	cp->c_data = buf;
	cp->c_ops = ops;
	c2_fom_init(&cp->c_fom, &cp_fom_type, &cp_fom_ops, NULL, NULL);

	C2_POST(c2_cm_cp_invariant(cp));
}

void c2_cm_cp_fini(struct c2_cm_cp *cp)
{
	c2_fom_fini(&cp->c_fom);
}

c2_bcount_t c2_cm_cp_size(struct c2_cm_cp *cp)
{
	return C2_CP_SIZE;
}

void c2_cm_cp_enqueue(struct c2_cm *cm, struct c2_cm_cp *cp)
{
}

/** @} end-of-DLD-cp-fspec */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
