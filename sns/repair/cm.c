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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSREPAIR
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/finject.h"

#include "mero/mero_setup.h"
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
     - @ref SNSRepairCMDLD-lspec-cm-data-next
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
  service. SNS Repair copy machine is typically started during Mero process
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
  - @ref SNSRepairCMDLD-lspec-cm-data-next
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
  outgoing buffer pools (m0_sns_repair_cm::rc_ibp and ::rc_obp).
  Both the buffer pools are initialised with colours equal to total number of
  localities in the request handler.
  After cm_setup() is successfully called, the copy machine transitions to
  M0_CMS_IDLE state and waits until failure happens. As mentioned in the HLD,
  failure information is a broadcast to all the replicas in the cluster using
  TRIGGER FOP. The FOM corresponding to the TRIGGER FOP activates the SNS Repair
  copy machine by invoking m0_cm_start(), this invokes SNS Repair specific start
  routine which initialises specific data structures.

  @subsection SNSRepairCMDLD-lspec-cm-start Copy machine startup
  The SNS Repair specific start routine provisions the buffer pools,
  viz. m0_sns_repair_cm::rc_ibp m0_sns_repair_cm::rc_obp with
  SNS_INCOMING_BUF_NR and SNS_OUTGOING_BUF_NR number of buffers.
  @note Buffer provisioning operation can block.

  @subsubsection SNSRepairCMDLD-lspec-cm-start-cp-create Copy packet create
  Once the buffer pools are provisioned, if resources permit (e.g. if there
  exist a free buffer in the outgoing SNS Repair buffer pool), Copy machine
  creates and initialises copy packets. Then by invoking m0_cm_data_next(),
  a copy packet is assigned an aggregation group and stobid. Once the copy
  packet is ready, an empty buffer is fetched from the outgoing buffer pool
  and attached to the copy packet (m0_cm_cp::c_data). Copy packet FOM
  (m0_cm_cp::c_fom) is then submitted to the request handler for further
  processing. Copy packets are created during startup and during finalisation
  of another completed copy packet.

  @see @ref CPDLD "Copy Packet DLD" for more details.

  @subsection SNSRepairCMDLD-lspec-cm-data-next Copy machine data iterator
  SNS Repair implements an iterator to efficiently select next data to process.
  This is done by implementing the copy machine specific operation, m0_cm_ops::
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
  implements copy machine state machine using generic Mero state machine
  infrastructure. @ref State machine <!-- sm/sm.h -->

  Locking
  All the updates to members of copy machine are done with m0_cm_lock() held.

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
  using cob name space iterator and pdclust layout infrastructure to select the
  next data to be repaired from the failure set. This is done in GOB fid and
  parity group order.

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
	 * Minimum number of buffers to provision m0_sns_repair_cm::rc_ibp
	 * and m0_sns_repair_cm::rc_obp buffer pools.
	 */
	SNS_INCOMING_BUF_NR = 1 << 6,
	SNS_OUTGOING_BUF_NR = 1 << 6
};

extern struct m0_net_xprt m0_net_lnet_xprt;
extern struct m0_cm_type  sns_repair_cmt;

M0_INTERNAL struct m0_sns_repair_cm *cm2sns(struct m0_cm *cm)
{
	return container_of(cm, struct m0_sns_repair_cm, rc_base);
}

M0_INTERNAL int m0_sns_repair_cm_type_register(void)
{
	return m0_cm_type_register(&sns_repair_cmt);
}

M0_INTERNAL void m0_sns_repair_cm_type_deregister(void)
{
	m0_cm_type_deregister(&sns_repair_cmt);
}

M0_INTERNAL struct m0_net_buffer *m0_sns_repair_buffer_get(struct
							   m0_net_buffer_pool
							   *bp, uint64_t colour)
{
	struct m0_net_buffer *buf;
	int                   i;

	m0_net_buffer_pool_lock(bp);
	M0_ASSERT(m0_net_buffer_pool_invariant(bp));
	buf = m0_net_buffer_pool_get(bp, colour);
	if (buf != NULL) {
		for (i = 0; i < SNS_SEG_NR; ++i)
			memset(buf->nb_buffer.ov_buf[i], 0, SNS_SEG_SIZE);
	}
	m0_net_buffer_pool_unlock(bp);

	return buf;
}

M0_INTERNAL void m0_sns_repair_buffer_put(struct m0_net_buffer_pool *bp,
					  struct m0_net_buffer *buf,
					  uint64_t colour)
{
	m0_net_buffer_pool_lock(bp);
	m0_net_buffer_pool_put(bp, buf, colour);
	m0_net_buffer_pool_unlock(bp);
}

static struct m0_cm_cp *cm_cp_alloc(struct m0_cm *cm)
{
	struct m0_sns_repair_cp *rcp;

	M0_ALLOC_PTR(rcp);
	if (rcp == NULL)
		return NULL;
	rcp->rc_base.c_ops = &m0_sns_repair_cp_ops;
	return &rcp->rc_base;
}

static void bp_below_threshold(struct m0_net_buffer_pool *bp)
{
	/* Buffer pool is below threshold.  */
}

const struct m0_net_buffer_pool_ops bp_ops = {
	.nbpo_not_empty       = m0_net_domain_buffer_pool_not_empty,
	.nbpo_below_threshold = bp_below_threshold
};

static int cm_setup(struct m0_cm *cm)
{
	struct m0_reqh          *reqh;
	struct m0_net_domain    *ndom;
	struct m0_sns_repair_cm *rcm;
	uint64_t                 colours;
	int                      rc;

	M0_ENTRY("cm: %p", cm);

	rcm = cm2sns(cm);
	reqh = cm->cm_service.rs_reqh;
	/*
	 * Total number of colours in incoming and outgoing buffer pools is
	 * same as the total number of localities in the reqh fom domain.
	 */
	colours = m0_reqh_nr_localities(reqh);
	ndom = m0_cs_net_domain_locate(m0_cs_ctx_get(reqh),
				       m0_net_lnet_xprt.nx_name);
	/*
	 * XXX This should be fixed, buffer pool ops should be a parameter to
	 * m0_net_buffer_pool_init() as it is NULL checked in
	 * m0_net_buffer_pool_invariant().
	 */
	rcm->rc_ibp.nbp_ops = &bp_ops;
	rcm->rc_obp.nbp_ops = &bp_ops;
	rc = m0_net_buffer_pool_init(&rcm->rc_ibp, ndom,
				     M0_NET_BUFFER_POOL_THRESHOLD, SNS_SEG_NR,
				     SNS_SEG_SIZE, colours, M0_0VEC_SHIFT);
	if (rc == 0) {
		rc = m0_net_buffer_pool_init(&rcm->rc_obp, ndom,
					     M0_NET_BUFFER_POOL_THRESHOLD,
					     SNS_SEG_NR, SNS_SEG_SIZE,
					     colours, M0_0VEC_SHIFT);
		if (rc != 0)
			m0_net_buffer_pool_fini(&rcm->rc_ibp);
	}

	if (rc == 0)
		m0_chan_init(&rcm->rc_stop_wait);

	M0_LEAVE();
	return rc;
}

static size_t cm_buffer_pool_provision(struct m0_net_buffer_pool *bp,
				       size_t bufs_nr)
{
	size_t bnr;

	m0_net_buffer_pool_lock(bp);
	M0_ASSERT(m0_net_buffer_pool_invariant(bp));
	bnr = m0_net_buffer_pool_provision(bp, bufs_nr);
	m0_net_buffer_pool_unlock(bp);

	return bnr;
}

static int cm_start(struct m0_cm *cm)
{
	struct m0_sns_repair_cm *rcm;
	int                      bufs_nr;
	int                      rc;

	M0_ENTRY("cm: %p", cm);

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
	rc = m0_sns_repair_iter_init(rcm);
	if (rc == 0)
		m0_cm_sw_fill(cm);

	M0_LEAVE();
	return rc;
}

static int cm_stop(struct m0_cm *cm)
{
	struct m0_sns_repair_cm *rcm;

	M0_PRE(cm != NULL);

	rcm = cm2sns(cm);
	m0_sns_repair_iter_fini(rcm);

	return 0;
}

static void cm_fini(struct m0_cm *cm)
{
	struct m0_sns_repair_cm *rcm;

	M0_ENTRY("cm: %p", cm);

	rcm = cm2sns(cm);
	m0_net_buffer_pool_fini(&rcm->rc_ibp);
	m0_net_buffer_pool_fini(&rcm->rc_obp);

	M0_LEAVE();
}

static void cm_complete(struct m0_cm *cm)
{
	struct m0_sns_repair_cm *rcm;

	rcm = cm2sns(cm);
	m0_chan_signal(&rcm->rc_stop_wait);
}

/** Copy machine operations. */
const struct m0_cm_ops cm_ops = {
	.cmo_setup        = cm_setup,
	.cmo_start        = cm_start,
	.cmo_cp_alloc     = cm_cp_alloc,
	.cmo_data_next    = m0_sns_repair_iter_next,
	.cmo_complete     = cm_complete,
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
