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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/finject.h"

#include "mero/setup.h"
#include "net/net.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"
#include "reqh/reqh.h"
#include "rpc/rpc.h"
#include "cm/ag.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"

/**
  @page SNSCMDLD SNS copy machine DLD
  - @ref SNSCMDLD-ovw
  - @ref SNSCMDLD-def
  - @ref SNSCMDLD-req
  - @ref SNSCMDLD-depends
  - @ref SNSCMDLD-highlights
  - @subpage SNSCMDLD-fspec
  - @ref SNSCMDLD-lspec
     - @ref SNSCMDLD-lspec-cm-setup
     - @ref SNSCMDLD-lspec-cm-start
        - @ref SNSCMDLD-lspec-cm-start-cp-create
     - @ref SNSCMDLD-lspec-cm-data-next
     - @ref SNSCMDLD-lspec-cm-stop
  - @ref SNSCMDLD-conformance
  - @ref SNSCMDLD-ut
  - @ref SNSCMDLD-st
  - @ref SNSCMDLD-O
  - @ref SNSCMDLD-ref

  <hr>
  @section SNSCMDLD-ovw Overview
  This module implements sns copy machine using generic copy machine
  infrastructure. SNS copy machine is built upon the request handler service.
  The same SNS copy machine can be configures to perform multiple tasks,
  viz.repair and rebalance using parity de-clustering layout.
  SNS copy machine is typically started during Mero process startup, although
  it can also be started later.

  <hr>
  @section SNSCMDLD-def Definitions
    Please refer to "Definitions" section in "HLD of copy machine and agents" and
    "HLD of SNS Repair" in @ref SNSCMDLD-ref

  <hr>
  @section SNSCMDLD-req Requirements
  - @b r.sns.cm.buffer.acquire The implementation should efficiently provide
    buffers for the repair operation without any deadlock.

  - @b r.sns.cm.sliding.window The implementation should efficiently use
    various copy machine resources using sliding window during copy machine
    operation, e.g. memory, cpu, etc.

  - @b r.sns.cm.data.next The implementation should efficiently select next
    data to be processed without causing any deadlock or bottle neck.

  - @b r.sns.cm.report.progress The implementation should efficiently report
    overall progress of data restructuring and update corresponding layout
    information for restructured objects.

  - @b r.sns.cm.repair.trigger For repair, SNS copy machine should respond to
    triggers caused by various kinds of failures as mentioned in the HLD of SNS
    Repair.

  - @b r.sns.cm.repair.iter For repair, SNS copy machine iterator should iterate
    over parity group units on the survived COBs and accordingly calculate and
    write the lost data to spare units of the corresponding parity group.

  - @b r.sns.cm.rebalance.iter For rebalance, SNS copy machine iterator should
    iterate over the spare units of the repaired parity groups and copy the data
    from corresponding spare units to the target unit on the new device.

  <hr>
  @section SNSCMDLD-depends Dependencies
  - @b r.sns.cm.resources.manage It must be possible to efficiently manage and
    throttle resources.

    Please refer to "Dependencies" section in "HLD of copy machine and agents"
    and "HLD of SNS Repair" in @ref SNSCMDLD-ref

  <hr>
  @section SNSCMDLD-highlights Design Highlights
  - SNS copy machine uses request handler service infrastructure.
  - SNS copy machine specific data structure embeds generic copy machine and
    other sns repair specific objects.
  - SNS copy machine defines its specific aggregation group data structure which
    embeds generic aggregation group.
  - Once initialised SNS copy machine remains idle until failure happens.
  - SNS buffer pool provisioning is done when operation starts.
  - SNS copy machine creates copy packets only if free buffers are available in
    the outgoing buffer pool.
  - Failure triggers SNS copy machine to start repair operation.
  - Once repair operation is complete, the rebalance operation can start if
    there exist a new device corresponding to the lost device. Thus the same
    copy machine is configured to perform re-balance operation.
  - For rebalance, Each used spare unit corresponds to exactly one (data or
    parity) unit on the lost device. SNS copy machine uses the same layout as
    used during sns repair to map a spare unit to the target unit on new device.
    The newly added device may have a new UUID, but will have the same index in
    the pool and the COB identifiers of the failed device and the replacement
    device will also be the same. Thus for re-balance, the same indices of the
    lost data/parity units on the lost device are used to write on to the newly
    added device with the same COB identifier as the failed device.

  <hr>
  @section SNSCMDLD-lspec Logical specification
  - @ref SNSCMDLD-lspec-cm-setup
  - @ref SNSCMDLD-lspec-cm-start
     - @ref SNSCMDLD-lspec-cm-start-cp-create
  - @ref SNSCMDLD-lspec-cm-data-next
  - @ref SNSCMDLD-lspec-cm-stop

  @subsection SNSCMDLD-lspec-comps Component overview
  The focus of sns copy machine is to efficiently restructure (repair or
  re-balance) data in case of failures, viz. device, node, etc. The
  restructuring operation is split into various copy packet phases.

  @subsection SNSCMDLD-lspec-cm-setup Copy machine setup
  SNS copy machine service allocates and initialises the corresponding copy
  machine.
  @see @ref SNSRepairSVC "SNS Repair service" for details.
  Once the copy machine is initialised, as part of copy machine setup, SNS
  copy machine specific resources are initialised, viz. incoming and outgoing
  buffer pools (m0_sns_cm::sc_ibp and ::sc_obp).
  Both the buffer pools are initialised with colours equal to total number of
  localities in the request handler.
  After cm_setup() is successfully called, the copy machine transitions to
  M0_CMS_IDLE state and waits until failure happens. As mentioned in the HLD,
  failure information is a broadcast to all the replicas in the cluster using
  TRIGGER FOP. The FOM corresponding to the TRIGGER FOP activates the SNS copy
  machine to start repair operation by invoking m0_cm_start(), this invokes SNS
  copy machine specific start routine which initialises specific data structures.

  Once the repair operation is complete the same copy machine is used to perform
  re-balance operation, iff there exist a new device/s corresponding to the lost
  device/s. In re-balance operation the data from the spare units of the repaired
  parity groups is copied to the new device using the layout.

  @subsection SNSCMDLD-lspec-cm-start Copy machine startup
  The SNS specific start routine provisions the buffer pools, viz. m0_sns_cm::
  sc_ibp m0_sns_cm::sc_obp with SNS_INCOMING_BUF_NR and SNS_OUTGOING_BUF_NR
  number of buffers.
  @note Buffer provisioning operation can block.

  @subsubsection SNSCMDLD-lspec-cm-start-cp-create Copy packet create
  Once the buffer pools are provisioned, if resources permit (e.g. if there
  exist a free buffer in the outgoing buffer pool), Copy machine creates and
  initialises copy packets. Then by invoking m0_cm_data_next(), a copy packet is
  assigned an aggregation group and stobid. Once the copy packet is ready, an
  empty buffer is fetched from the outgoing buffer pool and attached to the copy
  packet (m0_cm_cp::c_data). Copy packet FOM (m0_cm_cp::c_fom) is then submitted
  to the request handler for further processing. Copy packets are created during
  startup and during finalisation of another completed copy packet.

  @see @ref CPDLD "Copy Packet DLD" for more details.

  @subsection SNSCMDLD-lspec-cm-data-next Copy machine data iterator
  SNS copy machine implements an iterator to efficiently select next data to
  process. This is done by implementing the copy machine specific operation,
  m0_cm_ops::cmo_data_next(). The following pseudo code illustrates the SNS data
  iterator for repair as well as re-balance operation,

  @code
   - for each GOB G in aux-db (in global fid order)
     - fetch layout L for G
     // proceed in parity group order.
     - for each parity group S, until eof of G
       - map group S to COB list
       // determine whether group S needs reconstruction.
       - if no COB.containerid is in the failure set continue to the next group
       // group has to be reconstructed, create copy packets for all local units
       - if REPAIR
         - for each data and parity unit U in S (0 <= U < N + K)
       - if RE-BALANCE
         - for each spare unit U in S (N + K < U <= N + 2K)
       - map (S, U) -> (COB, F) by L
           - if COB is local and COB.containerid does not belong to the failure set
             - fetch frame F of COB
             - create copy packet
  @endcode
  The above iterator iterates through each GOB in aggregation group (parity
  group) order, so that the copy packet transformation doesn't block. Thus for
  SNS repair operation, only the data/parity units from every parity group
  belonging to the lost device are iterated, where as for SNS re-balance
  operation only the spare units from the repaired parity groups are iterated.

  @subsection SNSCMDLD-lspec-cm-stop Copy machine stop
  Once all the COBs (i.e. component objects) corresponding to the GOBs (i.e
  global file objects) belonging to the failure set are re-structured (repair or
  re-balance) by every replica in the cluster successfully, the re-structuring
  operation is marked complete.

  @subsection SNSCMDLD-lspec-thread Threading and Concurrency Model
  SNS copy machine is implemented as a request handler service, thus it shares
  the request handler threading model and does not create its own threads. All
  the copy machine operations are performed in context of request handler
  threads.

  SNS copy machine uses generic copy machine infrastructure, which implements
  copy machine state machine using generic Mero state machine infrastructure.
  @ref State machine <!-- sm/sm.h -->

  Locking
  All the updates to members of copy machine are done with m0_cm_lock() held.

  @subsection SNSCMDLD-lspec-numa NUMA optimizations
  N/A

  <hr>
  @section SNSCMDLD-conformance Conformance
  @b i.sns.cm.buffer.acquire SNS copy machine implements its incoming and
  outgoing buffer pools. The outgoing buffer pool is used to create copy
  packets. The respective buffer pools are provisioned during the start of
  the copy machine operation.

  @b i.sns.cm.sliding.window SNS copy machine implements its specific sliding
  window operations to efficiently manage its resources and communicate with
  other copy machine replicas.

  @b i.sns.cm.data.next SNS copy machine implements a next function using cob
  name space iterator and pdclust layout infrastructure to select the next data
  to be repaired from the failure set. This is done in GOB fid and parity group
  order.

  @b i.sns.cm.report.progress Progress is reported using sliding window and
  layout updates.

  @b i.sns.cm.repair.trigger Various failures are reported through TRIGGER FOP,
  which create corresponding FOMs. FOMs invoke sns specific copy machine
  operations through generic copy machine interfaces which cause copy machine
  state transitions.

  @b r.sns.cm.repair.iter For repair, SNS copy machine iterator iterates over
   parity group units on the survived COBs and accordingly calculates and
   writes the lost data to spare units of the corresponding parity group using
   layout.

  @b r.sns.cm.rebalance.iter For rebalance, SNS copy machine iterator iterates
  over only the spare units of the repaired parity groups and copy the data
  to the corresponding target units on the new device.

  <hr>
  @section SNSCMDLD-ut Unit tests

  @subsection SNSCMDLD-ut-cp Copy packet specific tests

  @test Test01: If an aggregation group is having a single copy packet, then
  transformation function should be a NO-OP.

  @test Test02: Test if all copy packets of an aggregation group get collected.

  @test Test03: Test the transformation function.
  Input: 2 bufvec's src and dest to be XORed.
  Output: XORed output stored in dest bufvec.

  <hr>
  @section SNSCMDLD-st System tests
  N/A

  <hr>
  @section SNSCMDLD-O Analysis
  N/A

  <hr>
  @section SNSCMDLD-ref References
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
  @addtogroup SNSCM

  SNS copy machine implements a copy machine in-order to re-structure data
  efficiently in an event of a failure. It uses GOB (global file object)
  and COB (component object) infrastructure with parity de-clustering layout.

  @{
*/

enum {
	SNS_SEG_NR = 10,
	SNS_SEG_SIZE = 4096,
	/*
	 * Minimum number of buffers to provision m0_sns_cm::sc_ibp
	 * and m0_sns_cm::sc_obp buffer pools.
	 */
	SNS_INCOMING_BUF_NR = 1 << 4,
	SNS_OUTGOING_BUF_NR = 1 << 4
};

extern struct m0_net_xprt m0_net_lnet_xprt;
extern struct m0_cm_type  sns_cmt;

M0_INTERNAL struct m0_sns_cm *cm2sns(struct m0_cm *cm)
{
	return container_of(cm, struct m0_sns_cm, sc_base);
}

M0_INTERNAL int m0_sns_cm_type_register(void)
{
	return m0_cm_type_register(&sns_cmt);
}

M0_INTERNAL void m0_sns_cm_type_deregister(void)
{
	m0_cm_type_deregister(&sns_cmt);
}

M0_INTERNAL struct m0_net_buffer *m0_sns_cm_buffer_get(struct m0_net_buffer_pool
						       *bp, uint64_t colour)
{
	struct m0_net_buffer *buf;
	int                   i;

	m0_net_buffer_pool_lock(bp);
	M0_ASSERT(m0_net_buffer_pool_invariant(bp));
	buf = m0_net_buffer_pool_get(bp, colour);
	if (buf != NULL) {
		for (i = 0; i < bp->nbp_seg_nr; ++i)
			memset(buf->nb_buffer.ov_buf[i], 0, bp->nbp_seg_size);
	}
	m0_net_buffer_pool_unlock(bp);

	return buf;
}

M0_INTERNAL void m0_sns_cm_buffer_put(struct m0_net_buffer_pool *bp,
					  struct m0_net_buffer *buf,
					  uint64_t colour)
{
	m0_net_buffer_pool_lock(bp);
	m0_net_buffer_pool_put(bp, buf, colour);
	m0_net_buffer_pool_unlock(bp);
}

static struct m0_cm_cp *cm_cp_alloc(struct m0_cm *cm)
{
	struct m0_sns_cm_cp *scp;

	M0_ALLOC_PTR(scp);
	if (scp == NULL)
		return NULL;
	scp->sc_base.c_ops = &m0_sns_cm_cp_ops;

	return &scp->sc_base;
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
	struct m0_reqh        *reqh;
	struct m0_net_domain  *ndom;
	struct m0_sns_cm      *scm;
	uint64_t               colours;
	m0_bcount_t            segment_size;
	uint32_t               segments_nr;
	int                    rc;

	M0_ENTRY("cm: %p", cm);

	scm = cm2sns(cm);
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
	scm->sc_ibp.nbp_ops = &bp_ops;
	scm->sc_obp.nbp_ops = &bp_ops;
	segment_size = m0_rpc_max_seg_size(ndom);
	segments_nr  = m0_rpc_max_segs_nr(ndom);
	rc = m0_net_buffer_pool_init(&scm->sc_ibp, ndom,
				     M0_NET_BUFFER_POOL_THRESHOLD, segments_nr,
				     segment_size, colours, M0_0VEC_SHIFT);
	if (rc == 0) {
		rc = m0_net_buffer_pool_init(&scm->sc_obp, ndom,
					     M0_NET_BUFFER_POOL_THRESHOLD,
					     segments_nr, segment_size,
					     colours, M0_0VEC_SHIFT);
		if (rc != 0)
			m0_net_buffer_pool_fini(&scm->sc_ibp);
	}

	if (rc == 0) {
		m0_mutex_init(&scm->sc_stop_wait_mutex);
		m0_chan_init(&scm->sc_stop_wait, &scm->sc_stop_wait_mutex);
	}

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

static int pm_event_setup_and_post(struct m0_poolmach *pm,
                                   enum m0_pool_event_owner_type et,
                                   uint32_t oid,
                                   enum m0_pool_nd_state state)
{
	struct m0_pool_event pme;

	M0_SET0(&pme);
	pme.pe_type  = et;
	pme.pe_index = oid;
	pme.pe_state = state;

	return m0_poolmach_state_transit(pm, &pme);
}

static int cm_start(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm;
	enum m0_pool_nd_state  pm_state;
	int                    bufs_nr;
	int                    rc;

	M0_ENTRY("cm: %p", cm);

	scm = cm2sns(cm);
	M0_ASSERT(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));

	bufs_nr = cm_buffer_pool_provision(&scm->sc_ibp, SNS_INCOMING_BUF_NR);
	if (bufs_nr == 0)
		return -ENOMEM;
	bufs_nr = cm_buffer_pool_provision(&scm->sc_obp, SNS_OUTGOING_BUF_NR);
	/*
	 * If bufs_nr is 0, then just return -ENOMEM, as cm_setup() was
	 * successful, both the buffer pools (incoming and outgoing) will be
	 * finalised in cm_fini().
	 */
	if (bufs_nr == 0)
		return -ENOMEM;

	pm_state = scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRING :
					      M0_PNDS_SNS_REBALANCING;
	if (pm_state == M0_PNDS_SNS_REPAIRING) {
		rc = pm_event_setup_and_post(cm->cm_pm, M0_POOL_DEVICE,
					     scm->sc_it.si_fdata,
					     M0_PNDS_FAILED);
		if (rc != 0)
			return rc;
	}

	rc = m0_sns_cm_iter_init(&scm->sc_it);
	if (rc == 0) {
		m0_cm_sw_fill(cm);
               rc = pm_event_setup_and_post(cm->cm_pm, M0_POOL_DEVICE,
					    scm->sc_it.si_fdata,
					    pm_state);

	}

	M0_LEAVE();
	return rc;
}

static int cm_stop(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm;
	enum m0_pool_nd_state  pm_state;

	M0_PRE(cm != NULL);

	scm = cm2sns(cm);
	M0_ASSERT(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));
	pm_state = scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRED :
					      M0_PNDS_SNS_REBALANCED;
	m0_sns_cm_iter_fini(&scm->sc_it);
        pm_event_setup_and_post(cm->cm_pm, M0_POOL_DEVICE, scm->sc_it.si_fdata,
                                pm_state);

	return 0;
}

static void cm_fini(struct m0_cm *cm)
{
	struct m0_sns_cm *scm;

	M0_ENTRY("cm: %p", cm);

	scm = cm2sns(cm);
	m0_net_buffer_pool_fini(&scm->sc_ibp);
	m0_net_buffer_pool_fini(&scm->sc_obp);

	m0_chan_fini_lock(&scm->sc_stop_wait);
	m0_mutex_fini(&scm->sc_stop_wait_mutex);

	M0_LEAVE();
}

static void cm_complete(struct m0_cm *cm)
{
	struct m0_sns_cm *scm;

	scm = cm2sns(cm);
	m0_chan_signal_lock(&scm->sc_stop_wait);
}

/** Copy machine operations. */
const struct m0_cm_ops cm_ops = {
	.cmo_setup         = cm_setup,
	.cmo_start         = cm_start,
	.cmo_cp_alloc      = cm_cp_alloc,
	.cmo_data_next     = m0_sns_cm_iter_next,
	.cmo_complete      = cm_complete,
	.cmo_stop          = cm_stop,
	.cmo_fini          = cm_fini
};

/** @} SNSCM */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
