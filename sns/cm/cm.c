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
#include "sns/cm/sns_ready_fop.h"

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
	SNS_INCOMING_BUF_NR = 1 << 7,
	SNS_OUTGOING_BUF_NR = 1 << 7,

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

static int pm_state(struct m0_sns_cm *scm)
{
	return scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRING :
					  M0_PNDS_SNS_REBALANCING;
}

static struct m0_rpc_machine *rpc_machine_find(struct m0_reqh *reqh){
        return m0_reqh_rpc_mach_tlist_head(&reqh->rh_rpc_machines);
}

static int cm_ready_post(struct m0_cm *cm)
{
        struct m0_reqh          *reqh = cm->cm_service.rs_reqh;
        struct m0_cm_proxy      *pxy;
        struct m0_cm_aggr_group *lo;
        struct m0_cm_aggr_group *hi;
        struct m0_cm_ag_id       ag_id;
        struct m0_rpc_machine   *rmach;
        const char              *ep;
        int                      rc;

        M0_ENTRY("cm: %p", cm);
        M0_PRE(cm != NULL);
        M0_PRE(m0_cm_is_locked(cm));

        rmach = rpc_machine_find(reqh);
	rc = m0_replicas_connect(cm, rmach, reqh);
        if (rc != 0) {
		/* There are no remote copy machine replicas. */
		if (rc == -ENOENT)
			rc = 0;
		return rc;
	}

        M0_SET0(&ag_id);
        rc = m0_cm_sw_update(cm);
        if(rc != 0)
                return rc;
        lo = m0_cm_ag_lo(cm);
        hi = m0_cm_ag_hi(cm);
        if (lo != NULL && hi != NULL) {
		ep = rmach->rm_tm.ntm_ep->nep_addr;
                m0_tl_for(proxy, &cm->cm_proxies, pxy) {
                        struct m0_fop *fop = m0_sns_cm_ready_fop_fill(cm,
					&lo->cag_id,
                                        &hi->cag_id, ep);
                        if (fop == NULL)
                                return -ENOMEM;
                        rc = m0_cm_ready_fop_post(fop, &pxy->px_conn);
                        m0_fop_put(fop);
                        if (rc != 0)
                                return rc;
                } m0_tl_endfor;
        }

        M0_LEAVE("rc: %d", rc);
        return rc;
}

static int cm_ready(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm = cm2sns(cm);
	int                    bufs_nr;
	int                    rc;
	int                    i;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(M0_IN(scm->sc_op, (SNS_REPAIR, SNS_REBALANCE)));

	bufs_nr = cm_buffer_pool_provision(&scm->sc_ibp.sb_bp,
					   SNS_INCOMING_BUF_NR);
	if (bufs_nr == 0)
		return -ENOMEM;
	bufs_nr = cm_buffer_pool_provision(&scm->sc_obp.sb_bp,
					   SNS_OUTGOING_BUF_NR);
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

	rc = cm_ready_post(cm);

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
	}

	return 0;
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

M0_INTERNAL bool m0_sns_cm_ag_is_relevant(struct m0_sns_cm *scm,
					  struct m0_pdclust_layout *pl,
					  const struct m0_cm_ag_id *id)
{
	struct m0_sns_cm_iter      *it = &scm->sc_it;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_pdclust_instance *pi;
	struct m0_fid               fid;
	struct m0_fid               cobfid;
	int                         rc;

	agid2fid(id,  &fid);
	rc = m0_sns_cm_fid_layout_instance(pl, &pi, &fid);
	if (rc == 0) {
		sa.sa_group = id->ai_lo.u_lo;
		sa.sa_unit = m0_pdclust_N(pl) + m0_pdclust_K(pl);
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &fid, &cobfid);
		m0_layout_instance_fini(&pi->pi_base);
		rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom, &cobfid);
		if (rc == 0 && !m0_sns_cm_is_cob_failed(scm, &cobfid))
			return true;
	}

	return false;
}

static bool sns_cm_fid_is_valid(const struct m0_fid *fid)
{
	return fid->f_container >= 0 && fid->f_key >= SNS_COB_FID_START;
}

/**
 * Returns true iff the copy machine has enough space to receive all
 * the copy packets from the given relevant group "id".
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
	uint64_t                  total_inbufs = 0;
	uint64_t                  cp_data_seg_nr;
	uint64_t                  nr_acc_bufs;
	uint64_t                  nr_incoming = 0;
	uint64_t                  nr_lu;
	bool                      result = false;

	M0_PRE(cm != NULL && id != NULL && pl != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &gfid);
	group = agid2group(id);
	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	nr_cp_bufs = m0_sns_cm_cp_buf_nr(&scm->sc_ibp.sb_bp, cp_data_seg_nr);
	nr_acc_bufs = nr_cp_bufs * m0_pdclust_K(pl);
	nr_lu = m0_sns_cm_ag_nr_local_units(scm, &gfid, pl, group);
	nr_incoming =  (m0_pdclust_N(pl) + m0_pdclust_K(pl)) -
			(nr_lu + scm->sc_failures_nr);
	M0_ASSERT(nr_incoming <= m0_pdclust_N(pl) + m0_pdclust_K(pl));
	total_inbufs = nr_acc_bufs + (nr_cp_bufs * nr_incoming);
	m0_net_buffer_pool_lock(&scm->sc_ibp.sb_bp);
	if (total_inbufs + m0_pdclust_N(pl) > scm->sc_ibp.sb_bp.nbp_free)
		goto out;
	if (scm->sc_ibp.sb_bp.nbp_free - (total_inbufs + m0_pdclust_N(pl)) > 0)
		result = true;
out:
	m0_net_buffer_pool_unlock(&scm->sc_ibp.sb_bp);
        M0_LOG(M0_DEBUG, "free buffers in: %u out: %u", scm->sc_ibp.sb_bp.nbp_free,
	       scm->sc_obp.sb_bp.nbp_free);

	return result;
}

static int cm_ag_next(struct m0_cm *cm, const struct m0_cm_ag_id *id_curr,
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
	uint64_t                  group = agid2group(id_curr);
	uint64_t                  i;
	int                       rc = 0;

	M0_PRE(cm != NULL && id_curr != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id_curr, &fid_curr);
	++group;
	if (!m0_fid_is_set(&fid_curr)) {
		group = 0;
		m0_fid_set(&fid_curr, 0, 4);
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
				if (!m0_sns_cm_has_space(cm, id_next, pl)) {
					M0_SET0(id_next);
					m0_layout_put(m0_pdl_to_layout(pl));
					return -ENOSPC;
				}
				if (pl != NULL)
					m0_layout_put(m0_pdl_to_layout(pl));
				*id_next = ag_id;
				return rc;
			}
		}
		group = 0;
		if (m0_fid_is_set(&fid_next) && sns_cm_fid_is_valid(&fid_next))
			fid_curr = fid_next;
		/* Increment fid_curr.f_key to fetch next fid. */
		M0_CNT_INC(fid_curr.f_key);
	} while ((rc = _fid_next(dbenv, cdom, &fid_curr, &fid_next)) == 0);

	return rc;
}
M0_INTERNAL bool m0_sns_cm_fid_repair_done(struct m0_fid *gfid,
					   struct m0_reqh *reqh)
{
	struct m0_sns_cm       *scm;
	struct m0_cm	       *cm;
	struct m0_reqh_service *service;
	struct m0_fid           curr_gfid;
	int			val;
	int			state;

	M0_PRE(gfid != NULL && reqh != NULL);

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
	val = m0_fid_cmp(gfid, &curr_gfid);
	return val < 0;
}

/** Copy machine operations. */
const struct m0_cm_ops cm_ops = {
	.cmo_setup         = cm_setup,
	.cmo_ready         = cm_ready,
	.cmo_start         = cm_start,
	.cmo_ag_alloc      = m0_sns_cm_ag_alloc,
	.cmo_cp_alloc      = cm_cp_alloc,
	.cmo_data_next     = m0_sns_cm_iter_next,
	.cmo_ag_next       = cm_ag_next,
	.cmo_complete      = cm_complete,
	.cmo_stop          = cm_stop,
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
