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
#include "reqh/reqh.h"
#include "rpc/rpc.h"
#include "cob/ns_iter.h"
#include "cm/proxy.h"

#include "sns/sns_addb.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/iter.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/ag.h"
#include "sns/cm/sw_onwire_fop.h"
#include "lib/locality.h"

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
     - @ref SNSCMDLD-lspec-cm-prepare
     - @ref SNSCMDLD-lspec-cm-start
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
  - @ref SNSCMDLD-lspec-cm-prepare
  - @ref SNSCMDLD-lspec-cm-start
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

  @subsection SNSCMDLD-lspec-cm-prepare Copy machine ready
  Allocates buffers for incoming and outgoing sns copy machine buffer pools.

  @subsection SNSCMDLD-lspec-cm-start Copy machine startup
  Starts and initialises sns copy machine data iterator.
  @see m0_sns_cm_iter_start()

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
	SNS_INCOMING_BUF_NR = 1 << 5,
	SNS_OUTGOING_BUF_NR = 1 << 5,

	/**
	 * Currently m0t1fs uses default fid_start = 4, where 0 - 3 are reserved
	 * for special purpose fids.
	 * @see @ref m0t1fs "fid_start part in overview section" for details.
	 */
	SNS_COB_FID_START = 4,
};

extern struct m0_net_xprt m0_net_lnet_xprt;
extern struct m0_cm_type  sns_repair_cmt;
extern struct m0_cm_type  sns_rebalance_cmt;
extern const struct m0_sns_cm_helpers repair_helpers;
extern const struct m0_sns_cm_helpers rebalance_helpers;

M0_INTERNAL struct m0_sns_cm *cm2sns(struct m0_cm *cm)
{
	return container_of(cm, struct m0_sns_cm, sc_base);
}

M0_INTERNAL int m0_sns_cm_type_register(void)
{
	int rc;

	rc = m0_cm_type_register(&sns_repair_cmt) ?:
	     m0_cm_type_register(&sns_rebalance_cmt);

	M0_RETURN(rc);
}

M0_INTERNAL void m0_sns_cm_type_deregister(void)
{
	m0_cm_type_deregister(&sns_repair_cmt);
	m0_cm_type_deregister(&sns_rebalance_cmt);
}

M0_INTERNAL struct m0_cm_cp *m0_sns_cm_cp_alloc(struct m0_cm *cm)
{
	struct m0_sns_cm_cp *scp;

	SNS_ALLOC_PTR(scp, &m0_sns_mod_addb_ctx, CP_ALLOC);
	if (scp == NULL)
		return NULL;

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

M0_INTERNAL int m0_sns_cm_setup(struct m0_cm *cm)
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
		M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_cm_buf_nr,
			     M0_ADDB_CTX_VEC(&m0_sns_cp_addb_ctx), 0, 0, 0, 0);
		rc = m0_sns_cm_iter_init(&scm->sc_it);
		if (rc != 0)
			return rc;
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

M0_INTERNAL int m0_sns_cm_pm_event_post(struct m0_sns_cm *scm,
					enum m0_pool_event_owner_type et,
					enum m0_pool_nd_state state)
{
	struct m0_poolmach         *pm = scm->sc_base.cm_pm;
	struct m0_pool_spare_usage *spare_array;
	struct m0_pooldev          *dev_array;
	uint32_t                    dev_id;
	bool                        transit;
	int                         i;
	int                         rc = 0;

	spare_array = pm->pm_state->pst_spare_usage_array;
	for (i = 0; spare_array[i].psu_device_index != POOL_PM_SPARE_SLOT_UNUSED; ++i) {
		transit = false;
		dev_id = spare_array[i].psu_device_index;
		dev_array = pm->pm_state->pst_devices_array;
		switch (state) {
		case M0_PNDS_SNS_REPAIRING:
			if (dev_array[dev_id].pd_state == M0_PNDS_FAILED)
				transit = true;
			break;
		case M0_PNDS_SNS_REPAIRED:
			if (dev_array[dev_id].pd_state ==
			    M0_PNDS_SNS_REPAIRING)
				transit = true;
			break;
		case M0_PNDS_SNS_REBALANCING:
			if (dev_array[dev_id].pd_state ==
			    M0_PNDS_SNS_REPAIRED)
				transit = true;
			break;
		case M0_PNDS_ONLINE:
			if (dev_array[dev_id].pd_state != M0_PNDS_SNS_REPAIRED)
				transit = true;
			break;
		default:
			M0_IMPOSSIBLE("Bad state");
			break;
		}
		if (transit) {
			struct m0_pool_event   pme;
			struct m0_be_tx_credit cred = {};
			struct m0_be_tx        tx;
			struct m0_sm_group    *grp  = m0_locality0_get()->lo_grp;

			M0_SET0(&pme);
			pme.pe_type  = et;
			pme.pe_index = dev_id;
			pme.pe_state = state;
			m0_sm_group_lock(grp);
			m0_be_tx_init(&tx, 0, scm->sc_it.si_beseg->bs_domain, grp,
					      NULL, NULL, NULL, NULL);
			m0_poolmach_store_credit(scm->sc_base.cm_pm, &cred);

			m0_be_tx_prep(&tx, &cred);
			/*
			 * XXX FIX ME: Replace m0_be_tx_{open, close}_sync()
			 * calls with corresponding async versions.
			 */
			rc = m0_be_tx_open_sync(&tx);
			if (rc == 0) {
				rc = m0_poolmach_state_transit(scm->sc_base.cm_pm,
							       &pme, &tx);
				m0_be_tx_close_sync(&tx);
			}
			m0_be_tx_fini(&tx);
			m0_sm_group_unlock(grp);
			if (rc != 0)
				break;
		}
	}

	return rc;
}

M0_INTERNAL int m0_sns_cm_prepare(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm = cm2sns(cm);
	int                    bufs_nr;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));

	if (scm->sc_ibp.sb_bp.nbp_buf_nr == 0 &&
	    scm->sc_obp.sb_bp.nbp_buf_nr == 0) {
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
		 * successful, both the buffer pools (incoming and outgoing)
		 * will be finalised in cm_fini().
		 */
		if (bufs_nr == 0)
			return -ENOMEM;
                M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_cm_buf_nr,
                             M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
			     scm->sc_ibp.sb_bp.nbp_buf_nr,
			     scm->sc_obp.sb_bp.nbp_buf_nr,
			     scm->sc_ibp.sb_bp.nbp_free,
			     scm->sc_obp.sb_bp.nbp_free);
	}

	M0_LEAVE();
	return 0;
}

M0_INTERNAL int m0_sns_cm_start(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm = cm2sns(cm);
	int                    rc;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));

	rc = m0_sns_cm_iter_start(&scm->sc_it);
	if (rc == 0)
		scm->sc_start_time = m0_time_now();

	M0_RETURN(rc);
}

M0_INTERNAL int m0_sns_cm_stop(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm = cm2sns(cm);;

	m0_sns_cm_iter_stop(&scm->sc_it);
	scm->sc_stop_time = m0_time_now();
	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_repair_info,
		     M0_ADDB_CTX_VEC(&m0_sns_mod_addb_ctx),
		     m0_time_sub(scm->sc_stop_time,scm->sc_start_time),
		     scm->sc_it.si_total_fsize);
	M0_CNT_INC(scm->sc_repair_done);
	return 0;
}

M0_INTERNAL void m0_sns_cm_fini(struct m0_cm *cm)
{
	struct m0_sns_cm *scm;

	M0_ENTRY("cm: %p", cm);

	scm = cm2sns(cm);
	m0_sns_cm_iter_fini(&scm->sc_it);
	m0_net_buffer_pool_fini(&scm->sc_ibp.sb_bp);
	m0_net_buffer_pool_fini(&scm->sc_obp.sb_bp);

	sns_cm_bp_fini(&scm->sc_obp);
	sns_cm_bp_fini(&scm->sc_ibp);

	M0_LEAVE();
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
	uint32_t              seg_nr = 0;
	uint32_t              rem_bufs;

	M0_PRE(m0_cm_is_locked(&scm->sc_base));

	colour =  cp_home_loc_helper(cp) % bp->nbp_colours_nr;
	rem_bufs = m0_sns_cm_cp_buf_nr(bp, cp->c_data_seg_nr);
	rem_bufs -= cp->c_buf_nr;
	while (rem_bufs > 0) {
		buf = m0_cm_buffer_get(bp, colour);
		if (buf == NULL)
			return -ENOBUFS;
		m0_cm_cp_buf_add(cp, buf);
		if (cp->c_data_seg_nr > (cp->c_buf_nr * bp->nbp_seg_nr))
			seg_nr = bp->nbp_seg_nr;
		else
			seg_nr = cp->c_data_seg_nr -
				 ((cp->c_buf_nr - 1) * bp->nbp_seg_nr);
		buf->nb_buffer.ov_vec.v_nr = seg_nr;
		M0_CNT_DEC(rem_bufs);
		if (!scp->sc_is_local) {
			if (scm->sc_ibp_reserved_nr > 0)
				M0_CNT_DEC(scm->sc_ibp_reserved_nr);
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

static int _fid_next(struct m0_cob_domain *cdom, struct m0_fid *fid_curr,
		     struct m0_fid *fid_next)
{
	int rc;

	rc = m0_cob_ns_next_of(cdom->cd_namespace, fid_curr,
			       fid_next);
	if (rc == 0)
		*fid_curr = *fid_next;
	return rc;
}

static bool sns_cm_fid_is_valid(const struct m0_fid *fid)
{
	return fid->f_container >= 0 && fid->f_key >= SNS_COB_FID_START;
}

M0_INTERNAL uint64_t
m0_sns_cm_incoming_reserve_bufs(struct m0_sns_cm *scm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl)
{
	struct m0_fid gfid;
	uint64_t      group;
	uint64_t      nr_cp_bufs;
	uint64_t      cp_data_seg_nr;
	uint64_t      nr_incoming;

	nr_incoming = m0_sns_cm_ag_max_incoming_units(scm, id, pl);
        agid2fid(id, &gfid);
        group = agid2group(id);
	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	nr_cp_bufs = m0_sns_cm_cp_buf_nr(&scm->sc_ibp.sb_bp, cp_data_seg_nr);

	return nr_cp_bufs * nr_incoming;
}

M0_INTERNAL void m0_sns_cm_normalize_reservation(struct m0_sns_cm *scm,
						 struct m0_cm_aggr_group *ag,
						 struct m0_pdclust_layout *pl,
						 uint64_t nr_res_bufs)
{
	uint64_t extra_bufs = 0;
	uint64_t nr_cp_bufs;
	uint64_t cp_data_seg_nr;

	M0_PRE(m0_cm_is_locked(&scm->sc_base));

	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	nr_cp_bufs = m0_sns_cm_cp_buf_nr(&scm->sc_ibp.sb_bp, cp_data_seg_nr);
	if (nr_res_bufs >= (nr_cp_bufs * (ag->cag_freed_cp_nr -
					  ag->cag_cp_local_nr)))
		extra_bufs = nr_res_bufs - (nr_cp_bufs * (ag->cag_freed_cp_nr -
							  ag->cag_cp_local_nr));
	if (extra_bufs > 0)
		scm->sc_ibp_reserved_nr -= extra_bufs;
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
M0_INTERNAL bool m0_sns_cm_has_space_for(struct m0_sns_cm *scm,
					 struct m0_pdclust_layout *pl,
					 uint64_t nr_bufs)
{
	bool result = false;

	M0_PRE(scm != NULL && pl != NULL);
	M0_PRE(m0_cm_is_locked(&scm->sc_base));

	m0_net_buffer_pool_lock(&scm->sc_ibp.sb_bp);
	if (nr_bufs + scm->sc_ibp_reserved_nr > scm->sc_ibp.sb_bp.nbp_free)
		goto out;
	scm->sc_ibp_reserved_nr += nr_bufs;
	result = true;
out:
	m0_net_buffer_pool_unlock(&scm->sc_ibp.sb_bp);
	M0_LOG(M0_DEBUG, "nr_bufs: [%lu] free buffers in: [%u] out: [%u] \
	       sc_ibp_reserved_nr: [%lu]", nr_bufs, scm->sc_ibp.sb_bp.nbp_free,
	       scm->sc_obp.sb_bp.nbp_free, scm->sc_ibp_reserved_nr);

	return result;
}

M0_INTERNAL int m0_sns_cm_ag_next(struct m0_cm *cm,
				  const struct m0_cm_ag_id id_curr,
				  struct m0_cm_ag_id *id_next)
{
	struct m0_sns_cm         *scm = cm2sns(cm);
	struct m0_fid             fid_start = {0, SNS_COB_FID_START};
	struct m0_fid             fid_curr;
	struct m0_fid             fid_next = {0, 0};
	struct m0_cm_ag_id        ag_id;
	struct m0_layout         *l;
	struct m0_pdclust_layout *pl = NULL;
	struct m0_cob_domain     *cdom = scm->sc_it.si_cob_dom;
	uint64_t                  fsize;
	uint64_t                  nr_gps = 0;
	uint64_t                  group = agid2group(&id_curr);
	uint64_t                  i;
	int                       rc = 0;

	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	if (!m0_cm_ag_id_is_set(&id_curr))
		fid_curr = fid_start;
	else {
		++group;
		agid2fid(&id_curr, &fid_curr);
	}
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
				l = m0_pdl_to_layout(pl);
				if (!cm->cm_ops->cmo_has_space(cm, &ag_id, l)) {
					M0_SET0(&ag_id);
					m0_layout_put(l);
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
	} while ((rc = _fid_next(cdom, &fid_curr, &fid_next)) == 0);

	return rc;
}

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
