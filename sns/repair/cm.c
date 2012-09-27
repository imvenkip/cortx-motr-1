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
 * Original creation date: 16/04/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_SNSREPAIR
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/finject.h"

#include "colibri/colibri_setup.h"
#include "net/net.h"
#include "reqh/reqh.h"
#include "cm/ag.h"
#include "sns/repair/cm.h"
#include "sns/repair/cp.h"

/**
  @page SNSRepairCMDLD SNS Repair copy machine DLD
  - @ref SNSRepairCMDLD-ovw
  - @ref SNSRepairCMDLD-def
  - @ref SNSRepairCMDLD-req
  - @ref SNSRepairCMDLD-depends
  - @ref SNSRepairCMDLD-highlights
  - @subpage SNSRepairCMDLD-fspec
  - @ref SNSRepairCMDLD-lspec
     - @ref SNSRepairCMDLD-lspec-cm-setup
     - @ref SNSRepairCMDLD-lspec-cm-start
        - @ref SNSRepairCMDLD-lspec-cm-start-cp-create
     - @ref SNSRepairCMDLD-lspec-cm-cp-data-next
     - @ref SNSRepairCMDLD-lspec-cm-stop
  - @ref SNSRepairCMDLD-conformance
  - @ref SNSRepairCMDLD-ut
  - @ref SNSRepairCMDLD-st
  - @ref SNSRepairCMDLD-O
  - @ref SNSRepairCMDLD-ref

  <hr>
  @section SNSRepairCMDLD-ovw Overview
  This module implements sns repair copy machine using generic copy machine
  infrastructure. SNS repair copy machine is built upon the request handler
  service. SNS Repair copy machine is typically started during Colibri process
  startup, although it can also be started later.

  <hr>
  @section SNSRepairCMDLD-def Definitions
    Please refer to "Definitions" section in "HLD of copy machine and agents" and
    "HLD of SNS Repair" in @ref SNSRepairCMDLD-ref

  <hr>
  @section SNSRepairCMDLD-req Requirements
  - @b r.sns.repair.trigger SNS repair copy machine should respond to triggers
    caused by various kinds of failures as mentioned in the HLD of SNS Repair.

  - @b r.sns.repair.buffer.acquire The implementation should efficiently provide
    buffers for the repair operation without any deadlock.

  - @b r.sns.repair.sliding.window The implementation should efficiently use
    various copy machine resources using sliding window during copy machine
    operation, e.g. memory, cpu, etc.

  - @b r.sns.repair.data.next The implementation should efficiently select next
    data to be processed without causing any deadlock or bottle neck.

  - @b r.sns.repair.report.progress The implementation should efficiently report
    overall progress of data restructuring and update corresponding layout
    information for restructured objects.

  <hr>
  @section SNSRepairCMDLD-depends Dependencies
  - @b r.sns.repair.resources.manage It must be possible to efficiently
    manage and throttle resources.

    Please refer to "Dependencies" section in "HLD of copy machine and agents"
    and "HLD of SNS Repair" in @ref SNSRepairCMDLD-ref

  <hr>
  @section SNSRepairCMDLD-highlights Design Highlights
  - SNS Repair copy machine uses request handler service infrastructure.
  - SNS Repair copy machine specific data structure embeds generic copy machine
    and other sns repair specific objects.
  - SNS Repair defines its specific aggregation group data structure which
    embeds generic aggregation group.
  - Once initialised SNS Repair copy machine remains idle until failure happens.
  - Failure triggers SNS Repair copy machine to start repair operation.
  - SNS Repair buffer pool provisioning is done when repair operation starts.
  - SNS Repair copy machine creates copy packets only if free buffers are
    available in the outgoing buffer pool.

  <hr>
  @section SNSRepairCMDLD-lspec Logical specification
  - @ref SNSRepairCMDLD-lspec-cm-setup
  - @ref SNSRepairCMDLD-lspec-cm-start
     - @ref SNSRepairCMDLD-lspec-cm-start-cp-create
  - @ref SNSRepairCMDLD-lspec-cm-cp-data-next
  - @ref SNSRepairCMDLD-lspec-cm-stop

  @subsection SNSRepairCMDLD-lspec-comps Component overview
  The focus of sns repair copy machine is to efficiently repair and restructure
  data in case of failures, viz. device, node, etc. The restructuring operation
  is split into various copy packet phases.

  @subsection SNSRepairCMDLD-lspec-cm-setup Copy machine setup
  SNS Repair service allocates and initialises the corresponding copy machine.
  @see @ref SNSRepairSVC "SNS Repair service" for details.
  Once the copy machine is initialised, as part of copy machine setup, SNS
  Repair copy machine specific resources are initialised, viz. incoming and
  outgoing buffer pools (c2_sns_repair_cm::rc_ibp and ::rc_obp) and cobfid_map.
  Both the buffer pools are initialised with colours equal to total number of
  localities in the request handler.
  After cm_setup() is successfully called, the copy machine transitions to
  C2_CMS_IDLE state and waits until failure happens. As mentioned in the HLD,
  failure information is a broadcast to all the replicas in the cluster using
  TRIGGER FOP. The FOM corresponding to the TRIGGER FOP activates the SNS Repair
  copy machine by invoking c2_cm_start(), this invokes SNS Repair specific start
  routine which initialises specific data structures.

  @subsection SNSRepairCMDLD-lspec-cm-start Copy machine startup
  The SNS Repair specific start routine provisions the buffer pools,
  viz. c2_sns_repair_cm::rc_ibp c2_sns_repair_cm::rc_obp with
  SNS_INCOMING_BUF_NR and SNS_OUTGOING_BUF_NR number of buffers.
  @note Buffer provisioning operation can block.

  @subsubsection SNSRepairCMDLD-lspec-cm-start-cp-create Copy packet create
  Once the buffer pools are provisioned, if resources permit (e.g. if there
  exist a free buffer in the outgoing SNS Repair buffer pool), Copy machine
  creates and initialises copy packets. Then by invoking c2_cm_data_next(),
  a copy packet is assigned an aggregation group and stobid. Once the copy
  packet is ready, an empty buffer is fetched from the outgoing buffer pool
  and attached to the copy packet (c2_cm_cp::c_data). Copy packet FOM
  (c2_cm_cp::c_fom) is then submitted to the request handler for further
  processing. Copy packets are created during startup and during finalisation
  of another completed copy packet.

  @see @ref CPDLD "Copy Packet DLD" for more details.

  @subsection SNSRepairCMDLD-lspec-cm-data-next Copy machine data iterator
  SNS Repair implements an iterator to efficiently select next data to process.
  This is done by implementing the copy machine specific operation, c2_cm_ops::
  cmo_data_next(). The following pseudo code illustrates the SNS Repair data
  iterator.

  @code
   - for each GOB G in aux-db (in global fid order)
     - fetch layout L for G
     // proceed in parity group order.
     - for each parity group S, until eof of G
       - map group S to COB list
       // determine whether group S needs reconstruction.
       - if no COB.containerid is in the failure set continue to the next group
       // group has to be reconstructed, create copy packets for all local units
       - for each data and parity unit U in S (0 <= U < N + K)
         - map (S, unit) -> (COB, F) by L
         - if COB is local and COB.containerid does not belong to the failure set
           - fetch frame F of COB
           - create copy packet
  @endcode
  The above iterator iterates through each GOB in aggregation group (parity
  group) order, so that the copy packet transformation doesn't block.

  @subsection SNSRepairCMDLD-lspec-cm-stop Copy machine stop
  Once all the COBs (i.e. component objects) corresponding to the GOBs
  (i.e global file objects) belonging to the failure set are repaired by every
  replica in the cluster successfully (including updating layouts), the repair
  operation is marked complete.

  @subsection SNSRepairCMDLD-lspec-thread Threading and Concurrency Model
  SNS Repair copy machine is implemented as a request handler service, thus it
  shares the request handler threading model and does not create its own
  threads. All the copy machine operations are performed in context of request
  handler threads.

  SNS Repair copy machine uses generic copy machine infrastructure, which
  implements copy machine state machine using generic Colibri state machine
  infrastructure. @ref State machine <!-- sm/sm.h -->

  Locking
  All the updates to members of copy machine are done with c2_cm_lock() held.

  @subsection SNSRepairCMDLD-lspec-numa NUMA optimizations
  N/A

  <hr>
  @section SNSRepairCMDLD-conformance Conformance
  @b i.sns.repair.trigger Various failures are reported through TRIGGER FOP,
  which create corresponding FOMs. FOMs invoke sns repair specific copy machine
  operations through generic copy machine interfaces which cause copy machine
  state transitions.

  @b i.sns.repair.buffer.acquire SNS Repair copy machine implements its incoming
  and outgoing buffer pools. The outgoing buffer pool is used to create copy
  packets. The respective buffer pools are provisioned during the start of the
  repair operation.

  @b i.sns.repair.sliding.window SNS Repair implements its specific sliding
  window operations to efficiently manage its resources and communicate with
  other copy machine replicas.

  @b i.sns.repair.data.next SNS Repair copy machine implements a next function
  using cobfid_map and pdclust layout infrastructure to select the next data
  to be repaired from the failure set. This is done in GOB fid and parity group
  order.

  @b i.sns.repair.report.progress Progress is reported using sliding window and
  layout updates.
  @todo Layout updates will be implemented at later stages of sns repair.

  <hr>
  @section SNSRepairCMDLD-ut Unit tests

  @subsection SNSRepairCMDLD-ut-cp Copy packet specific tests

  @test Test01: If an aggregation group is having a single copy packet, then
  transformation function should be a NO-OP.

  @test Test02: Test if all copy packets of an aggregation group get collected.

  @test Test03: Test the transformation function.
  Input: 2 bufvec's src and dest to be XORed.
  Output: XORed output stored in dest bufvec.

  <hr>
  @section SNSRepairCMDLD-st System tests
  N/A

  <hr>
  @section SNSRepairCMDLD-O Analysis
  N/A

  <hr>
  @section SNSRepairCMDLD-ref References
   Following are the references to the documents from which the design is
   derived,
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1FX-TTaM5VttwoG4w
   d0Q4-AbyVUi3XL_Oc6cnC4lxLB0/edit">Copy Machine redesign.</a>
   - <a href="https://docs.google.com/a/xyratex.com/document/d/1ZlkjayQoXVm-prMx
   Tkzxb1XncB6HU19I19kwrV-8eQc/edit?hl=en_US">HLD of copy machine and agents.</a
   >
   - <a href="https://docs.google.com/a/xyratex.com/Doc?docid=0ATg1HFjUZcaZZGNkN
   Xg4cXpfMTc5ZjYybjg4Y3Q&hl=en_US">HLD of SNS Repair.</a>
*/

/**
  @addtogroup SNSRepairCM

  SNS Repair copy machine implements a copy machine in-order to re-structure
  data efficiently in an event of a failure. It uses GOB (global file object)
  and COB (component object) infrastructure with parity de-clustering layout.

  @{
*/

enum {
	SNS_SEG_NR = 1,
	SNS_SEG_SIZE = 4096,
	/*
	 * Minimum number of buffers to provision c2_sns_repair_cm::rc_ibp
	 * and c2_sns_repair_cm::rc_obp buffer pools.
	 */
	SNS_INCOMING_BUF_NR = 1 << 16,
	SNS_OUTGOING_BUF_NR = 1 << 16
};

const struct c2_net_buffer_pool_ops bp_ops = {
        .nbpo_not_empty       = NULL,
        .nbpo_below_threshold = NULL,
};

extern struct c2_net_xprt c2_net_lnet_xprt;
extern struct c2_cm_type sns_repair_cmt;

struct c2_sns_repair_cm *cm2sns(struct c2_cm *cm)
{
	return container_of(cm, struct c2_sns_repair_cm, rc_base);
}

int c2_sns_repair_cm_type_register(void)
{
	return c2_cm_type_register(&sns_repair_cmt);
}

void c2_sns_repair_cm_type_deregister(void)
{
	c2_cm_type_deregister(&sns_repair_cmt);
}

struct c2_net_buffer *c2_sns_repair_buffer_get(struct c2_net_buffer_pool *bp,
					       size_t colour)
{
	struct c2_net_buffer *buf;

	C2_PRE(c2_net_buffer_pool_invariant(bp));

	c2_net_buffer_pool_lock(bp);
	buf = c2_net_buffer_pool_get(bp, colour);
	c2_net_buffer_pool_unlock(bp);

	return buf;
}

/* This is invoked from cm_data_next. */
static int cm_buf_attach(struct c2_cm *cm, struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cm *rcm;
	struct c2_net_buffer    *buf;
	size_t                   colour;

	rcm = cm2sns(cm);
	colour =  cp_home_loc_helper(cp) % rcm->rc_obp.nbp_colours_nr;
	buf = c2_sns_repair_buffer_get(&rcm->rc_obp, colour);
	if (buf == NULL)
		return -ENOMEM;
	cp->c_data = &buf->nb_buffer;

	return 0;
}

static struct c2_cm_cp *cm_cp_alloc(struct c2_cm *cm)
{
	struct c2_sns_repair_cp *rcp;

	C2_PRE(c2_cm_invariant(cm));

	C2_ALLOC_PTR(rcp);
	if (rcp == NULL)
		return NULL;
	rcp->rc_base.c_ops = &c2_sns_repair_cp_ops;
	return &rcp->rc_base;
}

static int cm_data_next(struct c2_cm *cm, struct c2_cm_cp *cp)
{
	/* XXX TODO: Iterate copy machine data. */

	cm_buf_attach(cm, cp);
	return 0;
}

static int cm_setup(struct c2_cm *cm)
{
	struct c2_reqh          *reqh;
	struct c2_net_domain    *ndom;
	struct c2_sns_repair_cm *rcm;
	uint64_t                 colours;
	int                      rc;

	C2_ENTRY("cm: %p", cm);

	rcm = cm2sns(cm);
	reqh = cm->cm_service.rs_reqh;
	/*
	 * Total number of colours in incoming and outgoing buffer pools is
	 * same as the total number of localities in the reqh fom domain.
	 */
	colours = c2_reqh_nr_localities(reqh);
	ndom = c2_cs_net_domain_locate(c2_cs_ctx_get(reqh),
				       c2_net_lnet_xprt.nx_name);
	/*
	 * XXX This should be fixed, buffer pool ops should be a parameter to
	 * c2_net_buffer_pool_init() as it is NULL checked in
	 * c2_net_buffer_pool_invariant().
	 */
	rcm->rc_ibp.nbp_ops = &bp_ops;
	rcm->rc_obp.nbp_ops = &bp_ops;
	rc = c2_net_buffer_pool_init(&rcm->rc_ibp, ndom,
				     C2_NET_BUFFER_POOL_THRESHOLD, SNS_SEG_NR,
				     SNS_SEG_SIZE, colours, C2_0VEC_SHIFT);
	if (rc == 0) {
		rc = c2_net_buffer_pool_init(&rcm->rc_obp, ndom,
					     C2_NET_BUFFER_POOL_THRESHOLD,
					     SNS_SEG_NR, SNS_SEG_SIZE,
					     colours, C2_0VEC_SHIFT);
		if (rc != 0)
			c2_net_buffer_pool_fini(&rcm->rc_ibp);
	}

	if (rc == 0)
		rc = c2_cobfid_map_get(reqh, &rcm->rc_cfm);

	C2_LEAVE();
	return rc;
}

static size_t cm_buffer_pool_provision(struct c2_net_buffer_pool *bp,
				       size_t bufs_nr)
{
	size_t bnr;

	C2_PRE(c2_net_buffer_pool_invariant(bp));

	c2_net_buffer_pool_lock(bp);
	bnr = c2_net_buffer_pool_provision(bp, bufs_nr);
	c2_net_buffer_pool_unlock(bp);

	return bnr;
}

static int cm_start(struct c2_cm *cm)
{
	struct c2_sns_repair_cm *rcm;
	int                      bufs_nr;

	C2_ENTRY("cm: %p", cm);

	rcm = cm2sns(cm);

	bufs_nr = cm_buffer_pool_provision(&rcm->rc_ibp, SNS_INCOMING_BUF_NR);
	if (bufs_nr == 0)
		return -ENOMEM;
	bufs_nr = cm_buffer_pool_provision(&rcm->rc_obp, SNS_OUTGOING_BUF_NR);
	/*
	 * If bufs_nr is 0, then just return -ENOMEM, as cm_setup() was
	 * successful, both the buffer pools (incoming and outgoing) will be
	 * finalised in cm_fini().
	 */
	if (bufs_nr == 0)
		return -ENOMEM;

	C2_LEAVE();
	return 0;
}

static int cm_stop(struct c2_cm *cm)
{
	C2_PRE(cm != NULL);

	return 0;
}

static void cm_fini(struct c2_cm *cm)
{
	struct c2_sns_repair_cm *rcm;

	C2_ENTRY("cm: %p", cm);

	rcm = cm2sns(cm);
	c2_net_buffer_pool_fini(&rcm->rc_ibp);
	c2_net_buffer_pool_fini(&rcm->rc_obp);
	c2_cobfid_map_put(cm->cm_service.rs_reqh);

	C2_LEAVE();
}

/** Copy machine operations. */
const struct c2_cm_ops cm_ops = {
	.cmo_setup        = cm_setup,
	.cmo_start        = cm_start,
	.cmo_cp_alloc     = cm_cp_alloc,
	.cmo_data_next    = cm_data_next,
	.cmo_stop         = cm_stop,
	.cmo_fini         = cm_fini
};

/** @} SNSRepairCM */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
