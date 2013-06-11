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

#include "fop/fop.h"

#include "mero/setup.h"
#include "net/net.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"
#include "reqh/reqh.h"
#include "rpc/rpc.h"
#include "cob/ns_iter.h"
#include "cm/proxy.h"

#include "sns/cm/cm_utils.h"
#include "sns/cm/iter.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/ag.h"
#include "sns/cm/sw_update_fop.h"

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
     - @ref SNSCMDLD-lspec-cm-ready
     - @ref SNSCMDLD-lspec-cm-start
        - @ref SNSCMDLD-lspec-cm-start-cp-create
     - @ref SNSCMDLD-lspec-cm-data-next
     - @ref SNSCMDLD-lspec-cm-sliding-window
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
  The same SNS copy machine can be configured to perform multiple tasks,
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
    buffers for the repair as well as re-balance operation without any deadlock.

  - @b r.sns.cm.sliding.window The implementation should efficiently use
    various copy machine resources using sliding window during copy machine
    operation, e.g. memory, cpu, etc.

  - @b r.sns.cm.sliding.window.init The implementation should efficiently
    communicate the initial sliding window to other replicas in the cluster.

  - @b r.sns.cm.sliding.window.update The implementation should efficiently
    update the sliding window to other replicas during repair.

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
  - Once initialised SNS copy machine remains idle until failure is reported.
  - SNS buffer pool provisioning is done when operation starts.
  - SNS copy machine creates copy packets only if free buffers are available in
    the outgoing buffer pool.
  - Failure triggers SNS copy machine to start repair operation.
  - For multiple nodes, SNS copy machine maintains a local proxy of every other
    remote replica in the cluster.
  - For multiple nodes, SNS copy machine calculates its initial sliding window
    and communicates it to other replicas identified by the local proxies through
    READY FOPs.
  - During the operation the sliding window updates are piggy backed along with
    the outgoing copy packets and their replies.
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
  - @ref SNSCMDLD-lspec-cm-ready
  - @ref SNSCMDLD-lspec-cm-start
     - @ref SNSCMDLD-lspec-cm-start-cp-create
  - @ref SNSCMDLD-lspec-cm-data-next
  - @ref SNSCMDLD-lspec-cm-sliding-window
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

  @subsection SNSCMDLD-lspec-cm-ready Copy machine ready
  After creating proxies representing the remote replicas, sns copy machine
  calculates its local sliding window (i.e lo and hi aggregation group
  identifiers). Then for each remote replica the READY FOPs are allocated and
  initialised with the calculated sliding window. The sns copy machine then
  broadcasts these READY FOPs to every remote replica using the rpc connection
  in the corresponding m0_cm_proxy. A READY FOP is a one-way fop and thus do not
  have a reply associated with it. Once every replica receives READY FOPs from
  all the corresponding remote replicas, the sns copy machine is ready to start
  the repair/re-balance operation.
  @see struct m0_cm_ready
  @see cm_ready_post()

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

  @subsection SNSCMDLD-lspec-cm-sliding-window Copy machine sliding window
  SNS copy machine implements sliding window using struct m0_cm::cm_aggr_grps_in
  list for aggregation groups having incoming copy packets.
  SNS copy machine implements the copy machine specific m0_cm::cmo_ag_next()
  operation to calculate the next relevant aggregation group identifier.
  Following algorithm illustrates the implementation of m0_cm::cmo_ag_next(),

  1) extract GOB (file identifier) G from the given aggregation group identifier A
  2) extract parity group identifier P from A
  3) increment P to process next group
  4) if G is valid (i.e. G is not any of the reserved file identifier e.g.
		    M0_COB_ROOT_FID)
	- fetch layout and file size for G
	- calculate total number of parity groups Sn for G
	- for each parity group P' until eof of G (p < p' < Sn)
	- setup aggregation group identifier A' using G and P
	- If P' is relevant aggregation group (has spare unit on any of the local
					       COBs)
	- If copy machine has space (has enough buffers for all the incoming copy
				     packets)
	- return A'
  5) else reset P to 0, fetch next G from aux-db and repeat from step 5

  m0_cm_ops::cmo_ag_next() is invoked from m0_cm_ag_advance() in a loop until
  m0_cm_ops::cmo_ag_next() returns valid next relevant aggregation group
  identifier.

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

  @b i.sns.cm.sliding.window SNS copy machine implements the sliding window
  using the struct m0_cm::cm_aggr_grps_in list for aggregation groups having
  incoming copy packets.

  @b i.sns.cm.sliding.window.init SNS copy machine calculates and communicates
  the initial sliding window in M0_CMS_READY phase through READY FOPs.

  @b i.sns.cm.sliding.window.update SNS copy machine piggy backs the sliding
  window with every outgoing copy packet during the operation.

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
	SNS_INCOMING_BUF_NR = 1 << 6,
	SNS_OUTGOING_BUF_NR = 1 << 6,

	/**
	 * Currently m0t1fs uses default fid_start = 4, where 0 - 3 are reserved
	 * for special purpose fids.
	 * @see @ref m0t1fs "fid_start part in overview section" for details.
	 */
	SNS_COB_FID_START = 4,
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
	.nbpo_not_empty       = m0_sns_cm_buf_available,
	.nbpo_below_threshold = bp_below_threshold
};

static void sns_cm_bp_init(struct m0_sns_cm_buf_pool *sbp)
{
	m0_mutex_init(&sbp->sb_wait_mutex);
	m0_chan_init(&sbp->sb_wait, &sbp->sb_wait_mutex);
}

static void sns_cm_bp_fini(struct m0_sns_cm_buf_pool *sbp)
{
	m0_chan_fini_lock(&sbp->sb_wait);
	m0_mutex_fini(&sbp->sb_wait_mutex);
}

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
	scm->sc_ibp.sb_bp.nbp_ops = &bp_ops;
	scm->sc_obp.sb_bp.nbp_ops = &bp_ops;
	segment_size = m0_rpc_max_seg_size(ndom);
	segments_nr  = m0_rpc_max_segs_nr(ndom);
	rc = m0_net_buffer_pool_init(&scm->sc_ibp.sb_bp, ndom,
				     M0_NET_BUFFER_POOL_THRESHOLD, segments_nr,
				     segment_size, colours, M0_0VEC_SHIFT);
	if (rc == 0) {
		rc = m0_net_buffer_pool_init(&scm->sc_obp.sb_bp, ndom,
					     M0_NET_BUFFER_POOL_THRESHOLD,
					     segments_nr, segment_size,
					     colours, M0_0VEC_SHIFT);
		if (rc != 0)
			m0_net_buffer_pool_fini(&scm->sc_ibp.sb_bp);
	}

	if (rc == 0) {
		rc = m0_sns_cm_iter_init(&scm->sc_it);
		if (rc != 0)
			return rc;
		m0_mutex_init(&scm->sc_wait_mutex);
		m0_chan_init(&scm->sc_wait, &scm->sc_wait_mutex);
		sns_cm_bp_init(&scm->sc_obp);
		sns_cm_bp_init(&scm->sc_ibp);
	}

	M0_LEAVE();
	return rc;
}

M0_INTERNAL size_t m0_sns_cm_buffer_pool_provision(struct m0_net_buffer_pool *bp,
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

static int pm_state(struct m0_sns_cm *scm)
{
	return scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRING :
					  M0_PNDS_SNS_REBALANCING;
}

static int cm_ready(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm = cm2sns(cm);
	int                    bufs_nr;
	int                    rc;
	int                    i;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));

	bufs_nr = m0_sns_cm_buffer_pool_provision(&scm->sc_ibp.sb_bp,
					      SNS_INCOMING_BUF_NR);
	M0_LOG(M0_DEBUG, "Got buffers in: [%d]", bufs_nr);
	if (bufs_nr == 0)
		return -ENOMEM;
	bufs_nr = m0_sns_cm_buffer_pool_provision(&scm->sc_obp.sb_bp,
					      SNS_OUTGOING_BUF_NR);
	M0_LOG(M0_DEBUG, "Got buffers out: [%d]", bufs_nr);
	/*
	 * If bufs_nr is 0, then just return -ENOMEM, as cm_setup() was
	 * successful, both the buffer pools (incoming and outgoing) will be
	 * finalised in cm_fini().
	 */
	if (bufs_nr == 0)
		return -ENOMEM;

	if (scm->sc_op == SNS_REPAIR) {
		M0_CNT_INC(scm->sc_failures_nr);
		for (i = 0; i < scm->sc_failures_nr; ++i) {
			rc = pm_event_setup_and_post(cm->cm_pm, M0_POOL_DEVICE,
						     scm->sc_it.si_fdata[i],
						     M0_PNDS_FAILED);
			if (rc != 0)
				return rc;
		}
	}

	M0_LEAVE();
	return rc;
}

static int cm_start(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm = cm2sns(cm);
	enum m0_pool_nd_state  state;
	int                    rc = 0;
	int                    i;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));

	state = pm_state(scm);
	m0_cm_continue(cm);
	for (i = 0; i < scm->sc_failures_nr; ++i) {
		rc = pm_event_setup_and_post(cm->cm_pm, M0_POOL_DEVICE,
					     scm->sc_it.si_fdata[i],
					     state);
	}

	rc = m0_sns_cm_iter_start(&scm->sc_it);

	M0_LEAVE();
	return rc;
}

static int cm_stop(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm;
	enum m0_pool_nd_state  pm_state;
	int                    i;

	M0_PRE(cm != NULL);

	scm = cm2sns(cm);
	M0_ASSERT(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));
	pm_state = scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRED :
					      M0_PNDS_SNS_REBALANCED;
	m0_sns_cm_iter_stop(&scm->sc_it);
	for (i = 0; i < scm->sc_failures_nr; ++i) {
		pm_event_setup_and_post(cm->cm_pm, M0_POOL_DEVICE,
					scm->sc_it.si_fdata[i], pm_state);
	}

	return 0;
}

static void cm_fini(struct m0_cm *cm)
{
	struct m0_sns_cm *scm;

	M0_ENTRY("cm: %p", cm);

	scm = cm2sns(cm);
	m0_sns_cm_iter_fini(&scm->sc_it);
	m0_net_buffer_pool_fini(&scm->sc_ibp.sb_bp);
	m0_net_buffer_pool_fini(&scm->sc_obp.sb_bp);

	m0_chan_fini_lock(&scm->sc_wait);
	m0_mutex_fini(&scm->sc_wait_mutex);

	sns_cm_bp_fini(&scm->sc_obp);
	sns_cm_bp_fini(&scm->sc_ibp);

	M0_LEAVE();
}

static void cm_complete(struct m0_cm *cm)
{
	struct m0_sns_cm *scm;

	scm = cm2sns(cm);
	m0_chan_signal_lock(&scm->sc_wait);
}

M0_INTERNAL uint64_t m0_sns_cm_cp_buf_nr(struct m0_net_buffer_pool *bp,
					 uint64_t data_seg_nr)
{
	return data_seg_nr % bp->nbp_seg_nr ?
	       data_seg_nr / bp->nbp_seg_nr + 1 :
	       data_seg_nr / bp->nbp_seg_nr;
}

M0_INTERNAL int m0_sns_cm_buf_attach(struct m0_net_buffer_pool *bp,
				     struct m0_cm_cp *cp)
{
	struct m0_net_buffer *buf;
	struct m0_sns_cm     *scm = cm2sns(cp->c_ag->cag_cm);
	struct m0_sns_cm_cp  *scp = cp2snscp(cp);
	size_t                colour;
	uint32_t              seg_nr;
	uint32_t              rem_bufs;

	M0_PRE(m0_cm_is_locked(&scm->sc_base));

	colour =  cp_home_loc_helper(cp) % bp->nbp_colours_nr;
	seg_nr = cp->c_data_seg_nr;
	rem_bufs = m0_sns_cm_cp_buf_nr(bp, seg_nr);
	rem_bufs -= cp->c_buf_nr;
	while (rem_bufs > 0) {
		buf = m0_cm_buffer_get(bp, colour);
		if (buf == NULL)
			return -ENOBUFS;
		m0_cm_cp_buf_add(cp, buf);
		M0_CNT_DEC(rem_bufs);
		if (!scp->sc_is_local) {
			if (scm->sc_ibp_reserved_nr > 0) {
				M0_CNT_DEC(scm->sc_ibp_reserved_nr);
			M0_LOG(M0_DEBUG, "id [%lu] [%lu] [%lu] [%lu] [%lu]", cp->c_ag->cag_id.ai_hi.u_hi, cp->c_ag->cag_id.ai_hi.u_lo,
			       cp->c_ag->cag_id.ai_lo.u_hi, cp->c_ag->cag_id.ai_lo.u_lo, scm->sc_ibp_reserved_nr);
			}
		}
	}

	return 0;
}

static void __buffer_pools_prune(struct m0_net_buffer_pool *bp, uint32_t buf_nr)
{
	M0_PRE(bp != NULL && buf_nr < bp->nbp_buf_nr);

	m0_net_buffer_pool_lock(bp);
	while (buf_nr > 0) {
		m0_net_buffer_pool_prune(bp);
		M0_CNT_DEC(buf_nr);
	}
	m0_net_buffer_pool_unlock(bp);

}

M0_INTERNAL void m0_sns_cm_buffer_pools_prune(struct m0_cm *cm)
{
	struct m0_sns_cm *scm;
	uint32_t buf_nr;

	M0_PRE(cm != NULL);

	scm = cm2sns(cm);
	if (scm->sc_ibp.sb_bp.nbp_buf_nr > SNS_INCOMING_BUF_NR &&
		scm->sc_ibp.sb_bp.nbp_free > scm->sc_ibp_reserved_nr) {
		buf_nr = scm->sc_ibp.sb_bp.nbp_free - (scm->sc_ibp_reserved_nr / 2);
		__buffer_pools_prune(&scm->sc_ibp.sb_bp, buf_nr);
	}
}

M0_INTERNAL uint64_t m0_sns_cm_data_seg_nr(struct m0_sns_cm *scm,
					   struct m0_pdclust_layout *pl)
{
	M0_PRE(scm != NULL && pl != NULL);

	return m0_pdclust_unit_size(pl) %
	       scm->sc_obp.sb_bp.nbp_seg_size ?
	       m0_pdclust_unit_size(pl) /
	       scm->sc_obp.sb_bp.nbp_seg_size + 1 :
	       m0_pdclust_unit_size(pl) /
	       scm->sc_obp.sb_bp.nbp_seg_size;
}

static int _fid_next(struct m0_dbenv *dbenv, struct m0_cob_domain *cdom,
		    struct m0_fid *fid_curr, struct m0_fid *fid_next)
{
	int             rc;
	struct m0_db_tx tx;

	rc = m0_db_tx_init(&tx, dbenv, 0);
	if (rc != 0)
		return rc;

	rc = m0_cob_ns_next_of(&cdom->cd_namespace, &tx, fid_curr,
			       fid_next);
	if (rc == 0) {
		m0_db_tx_commit(&tx);
		*fid_curr = *fid_next;
	} else
		m0_db_tx_abort(&tx);

	return rc;
}

static bool sns_cm_fid_is_valid(const struct m0_fid *fid)
{
	return fid->f_container >= 0 && fid->f_key >= SNS_COB_FID_START;
}

M0_INTERNAL void m0_sns_cm_normalize_reservation(struct m0_cm *cm,
						 struct m0_cm_aggr_group *ag)
{
	struct m0_pdclust_layout *pl;
	struct m0_sns_cm         *scm;
	uint64_t                  nr_cp_bufs;
	uint64_t                  cp_data_seg_nr;
	uint32_t                  res_bufs;
	uint64_t                  nr_acc_bufs;
	uint64_t                  nr_incoming;
	uint32_t                  dpupg;
	uint32_t                  actual_freed = 0;

	pl = m0_layout_to_pdl(ag->cag_layout);
	scm = cm2sns(cm);
	dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl) - scm->sc_failures_nr;
	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	nr_cp_bufs = m0_sns_cm_cp_buf_nr(&scm->sc_ibp.sb_bp, cp_data_seg_nr);
	nr_acc_bufs = nr_cp_bufs * m0_pdclust_K(pl);
	nr_incoming = dpupg - ag->cag_cp_local_nr;
	res_bufs = nr_acc_bufs + (nr_cp_bufs * nr_incoming);
	if (res_bufs >= ag->cag_freed_cp_nr)
		actual_freed = res_bufs - (nr_cp_bufs * (ag->cag_freed_cp_nr - ag->cag_cp_local_nr));
	if (actual_freed > 0)
		scm->sc_ibp_reserved_nr -= actual_freed;
}

/**
 * Returns true iff the copy machine has enough space to receive all
 * the copy packets from the given relevant group "id".
 * Reserves buffers from incoming buffer pool struct m0_sns_cm::sc_ibp
 * corresponding to all the incoming copy packets.
 * e.g. sns repair copy machine checks if the incoming buffer pool has
 * enough free buffers to receive all the remote units corresponding
 * to a parity group.
 */
M0_INTERNAL bool m0_sns_cm_has_space(struct m0_cm *cm, const struct m0_cm_ag_id *id,
				     struct m0_pdclust_layout *pl)
{
	struct m0_sns_cm         *scm = cm2sns(cm);
	struct m0_fid             gfid;
	uint64_t                  group;
	uint64_t                  nr_cp_bufs;
	uint64_t                  total_inbufs;
	uint64_t                  cp_data_seg_nr;
	uint64_t                  nr_acc_bufs;
	uint64_t                  nr_incoming;
	uint64_t                  nr_lu;
	uint32_t                  dpupg;
	bool                      result = false;

	M0_PRE(cm != NULL && id != NULL && pl != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &gfid);
	group = agid2group(id);
	dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl) - scm->sc_failures_nr;
	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	/*
	 * Calculate number of buffers required for a copy packet.
	 * This depends on the unit size and the max buffer size.
	 */
	nr_cp_bufs = m0_sns_cm_cp_buf_nr(&scm->sc_ibp.sb_bp, cp_data_seg_nr);
	/* Calculate number of buffers required for accumulator copy packets. */
	nr_acc_bufs = nr_cp_bufs * m0_pdclust_K(pl);
	/* Calculate number of incoming copy packets for this aggregation group. */
	nr_lu = m0_sns_cm_ag_nr_local_units(scm, &gfid, pl, group);
	nr_incoming = dpupg - nr_lu;
	M0_ASSERT(nr_incoming <= dpupg);
	/*
	 * Calculate total number of buffers required to be available for all
	 * the incoming copy packets of this aggregation group.
	 * Note: Here we simply reserve buffers corresponding to
	 * N + K - failures - local data units copy packets. This may lead to
	 * extra reservation of buffers as there's a possibility that a node
	 * may host multiple outgoing units of the same aggregation group. In
	 * this case the multiple outgoing units are transformed into single
	 * copy packet by the sender. Thus receiver instead of receiving data
	 * in multiple copy packets as expected, receives the data in a single
	 * copy packet.
	 * We normalize this extra buffer reservation later during the
	 * aggregation group is finalised at the receiver.
	 * see function sns_cm_normalize_reservation()
	 */
	total_inbufs = nr_acc_bufs + (nr_cp_bufs * nr_incoming);
	m0_net_buffer_pool_lock(&scm->sc_ibp.sb_bp);
	if (total_inbufs + scm->sc_ibp_reserved_nr > scm->sc_ibp.sb_bp.nbp_free) {
		if (total_inbufs + scm->sc_ibp_reserved_nr > scm->sc_ibp.sb_bp.nbp_free)
				goto out;
	}
	scm->sc_ibp_reserved_nr += total_inbufs;
	result = true;
out:
	m0_net_buffer_pool_unlock(&scm->sc_ibp.sb_bp);
	M0_LOG(M0_DEBUG, "free buffers in: [%u] out: [%u] \
	       sc_ibp_reserved_nr: [%lu]", scm->sc_ibp.sb_bp.nbp_free,
	       scm->sc_obp.sb_bp.nbp_free, scm->sc_ibp_reserved_nr);

	return result;
}

static int cm_ag_next(struct m0_cm *cm, const struct m0_cm_ag_id id_curr,
		      struct m0_cm_ag_id *id_next)
{
	struct m0_sns_cm         *scm = cm2sns(cm);
	struct m0_fid             fid_curr;
	struct m0_fid             fid_next = {0, 0};
	struct m0_cm_ag_id        ag_id;
	struct m0_pdclust_layout *pl = NULL;
	struct m0_dbenv          *dbenv = scm->sc_it.si_dbenv;
	struct m0_cob_domain     *cdom = scm->sc_it.si_cob_dom;
	uint64_t                  fsize;
	uint64_t                  nr_gps = 0;
	uint64_t                  group = agid2group(&id_curr);
	uint64_t                  i;
	int                       rc = 0;

	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(&id_curr, &fid_curr);
	++group;
	do {
		if (sns_cm_fid_is_valid(&fid_curr)) {
			rc = m0_sns_cm_file_size_layout_fetch(cm, &fid_curr,
							      &pl, &fsize);
			if (rc != 0)
				return rc;
			nr_gps = m0_sns_cm_nr_groups(pl, fsize);
			for (i = group; i < nr_gps; ++i) {
				m0_sns_cm_ag_agid_setup(&fid_curr, i, &ag_id);
				if (!m0_sns_cm_ag_is_relevant(scm, pl, &ag_id))
					continue;
				if (!m0_sns_cm_has_space(cm, &ag_id, pl)) {
					M0_SET0(&ag_id);
					m0_layout_put(m0_pdl_to_layout(pl));
					return -ENOSPC;
				}
				if (pl != NULL)
					m0_layout_put(m0_pdl_to_layout(pl));
				*id_next = ag_id;
				return rc;
			}
			m0_layout_put(m0_pdl_to_layout(pl));
		}
		group = 0;
		if (m0_fid_is_set(&fid_next) && sns_cm_fid_is_valid(&fid_next))
			fid_curr = fid_next;
		/* Increment fid_curr.f_key to fetch next fid. */
		M0_CNT_INC(fid_curr.f_key);
	} while ((rc = _fid_next(dbenv, cdom, &fid_curr, &fid_next)) == 0);

	return rc;
}

M0_INTERNAL enum sns_repair_state
m0_sns_cm_fid_repair_done(struct m0_fid *gfid, struct m0_reqh *reqh)
{
	struct m0_sns_cm       *scm;
	struct m0_cm	       *cm;
	struct m0_reqh_service *service;
	struct m0_fid           curr_gfid;
	int			state;

	M0_PRE(gfid != NULL && m0_fid_is_valid(gfid));
	M0_PRE(reqh != NULL);

	service = m0_reqh_service_find(&sns_cmt.ct_stype, reqh);
	M0_ASSERT(service != NULL);

	cm = container_of(service, struct m0_cm, cm_service);
	scm = cm2sns(cm);

	M0_SET0(&curr_gfid);
	m0_cm_lock(cm);
	state = m0_cm_state_get(cm);
	if (state == M0_CMS_ACTIVE)
		curr_gfid = scm->sc_it.si_fc.sfc_gob_fid;
	m0_cm_unlock(cm);
	if (curr_gfid.f_container == 0 && curr_gfid.f_key == 0)
		return SRS_UNINITIALIZED;
	return m0_fid_cmp(gfid, &curr_gfid) > 0 ? SRS_REPAIR_NOTDONE :
	       SRS_REPAIR_DONE;
}

/** Copy machine operations. */
const struct m0_cm_ops cm_ops = {
	.cmo_setup               = cm_setup,
	.cmo_ready               = cm_ready,
	.cmo_start               = cm_start,
	.cmo_ag_alloc            = m0_sns_cm_ag_alloc,
	.cmo_cp_alloc            = cm_cp_alloc,
	.cmo_data_next           = m0_sns_cm_iter_next,
	.cmo_ag_next             = cm_ag_next,
	.cmo_sw_update_fop_alloc = m0_sns_cm_sw_update_fop_alloc,
	.cmo_complete            = cm_complete,
	.cmo_stop                = cm_stop,
	.cmo_fini          = cm_fini
};

#undef M0_TRACE_SUBSYSTEM

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
