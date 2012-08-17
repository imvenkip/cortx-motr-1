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

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/finject.h"

#include "colibri/colibri_setup.h"
#include "net/net.h"
#include "reqh/reqh.h"
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
  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#heading=h.4493e2a5a920

  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1ZlkjayQoXVm-prMxTkzxb1XncB6HU19I19kwrV-8eQc/edit#heading=h.8c396c004f1f>


  <hr>
  @section SNSRepairCMDLD-req Requirements
  - @b r.sns.repair.trigger SNS repair copy machine should respond to triggers
    caused by various kinds of failures as mentioned in the HLD of SNS Repair.

  - @b r.sns.repair.buffer.acquire The implementation should efficiently provide
    buffers for the repair operation without any deadlock.

  - @b r.sns.repair.sliding.window The implementation should efficiently use
    various copy machine resources using sliding window during copy machine
    operation, e.g. memory, cpu, &c.

  - @b r.sns.repair.data.next The implementation should efficiently select next
    data to be processed without causing any deadlock or bottle neck.

  - @b r.sns.repair.report.progress The implementation should efficiently report
    overall progress of data restructuring and update corresponding layout
    information for restructured objects.

  <hr>
  @section SNSRepairCMDLD-depends Dependencies
  - @b r.sns.repair.resources.manage It must be possible to efficiently
    manage and throttle resources.

  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#heading=h.c7533697f11c

  <hr>
  @section SNSRepairCMDLD-highlights Design Highlights
  - SNS Repair copy machine uses request handler service infrastructure.
  - SNS Repair copy machine specific data structure embeds generic copy machine
    and other sns repair specific objects.
  - SNS Repair defines its specific aggregation group data structure which
    embeds generic aggregation group.
  - Once initialised SNS Repair copy machine remains idle until failure happens.
  - Failure triggers SNS Repair copy machine to start repair operation.
  - SNS Repair buffer pool provisioning is done when repair operation is starts.
  - Every SNS Repair replica in the cluster broadcasts its sliding window size
    before starting its operation using READY FOPs.

  <hr>
  @section SNSRepairCMDLD-lspec Logical specification
  - @ref SNSRepairCMDLD-lspec-cm-setup
  - @ref SNSRepairCMDLD-lspec-cm-start
  - @ref SNSRepairCMDLD-lspec-cm-start-cp-create
  - @ref SNSRepairCMDLD-lspec-cm-stop

  @subsection SNSRepairCMDLD-lspec-comps Component overview
  The focus of sns repair copy machine is on restructuring the data
  efficiently. The restructuring operation is split into various copy packet
  phases.

  @subsection SNSRepairCMDLD-lspec-cm-setup Copy machine setup
  SNS Repair defines its following specific data structure to represent a
  copy machine.
  @code
  struct c2_sns_repair_cm {
          struct c2_cm               rc_base;
          struct c2_net_buffer_pool  rc_pool; 
  };
  @endcode
  SNS Repair service allocates and initialises the corresponding copy machine.
  @see @ref SNSRepairSVC "SNS Repair service" for details.
  Once the copy machine is initialised it remains idle until failure happens.
  As mentioned in the HLD, failure information is a broadcast to all the
  replicas in the cluster using TRIGGER FOP. The FOM corresponding to the
  TRIGGER FOP activates the SNS Repair copy machine by invoking c2_cm_start(),
  this invokes SNS Repair specific start routine which initialises specific
  data structures.
  @code
  int c2_cm_start(struct c2_cm *cm)
  {
    ...
    cm->cm_ops->cmo_start(cm);
    ...
  }
  @endcode

  @subsection SNSRepairCMDLD-lspec-cm-start Copy machine startup
  SNS Repair specific start routine initialises the buffer pool,
  viz. c2_sns_repair_cm::rc_pool and provisions it with some N number of
  buffers. Once the buffer provisioning is done, copy machine updates the
  sliding window size to the size of its buffer pool and broadcasts the same
  to other replicas in the cluster. Similarly other replicas broadcast their
  corresponding sliding window sizes. After receiving sliding window sizes
  from each replica in the cluster, every SNS Repair copy machine selects
  the minimum of the sliding window sizes in the cluster and updates its
  local sliding window size accordingly.
  @note Buffer provisioning operation can block.
 
  @subsubsection SNSRepairCMDLD-lspec-cm-start-cp-create Copy packet create
  Once the sliding window size is updated to minimum within the cluster, the
  copy machine checks if sliding window has space (c2_cm_sw_ops::swo_has_space()
  and accordingly creates copy packets. Every newly created copy packet is
  attached with a blank data buffer and corresponding copy packet FOM is
  submitted to the request handler for further processing.
  Following pseudo code illustrates the copy packet creation,

  @code
  int c2_cm_cp_create(struct c2_cm *cm)
  {
    ...
    // Check if sliding window has space and create copy packets.
    while (sw->sw_ops->swo_has_space(sw)) {
           cp = cm->cm_ops->cmo_cp_alloc(cm);
            if (cp == NULL)
	        return -ENOMEM;
           c2_cm_cp_enqueue(cm, cp);
    }
    ...
  }

  cm_start(struct c2_cm *cm)
  {
    struct c2_cm_cp *cp;
    ...
    cp = c2_cm_cp_create(cm);
    ...
  }
  @endcode
             
  Further copy packet header details, required for the operation are populated as
  part of copy packet FOM init phase asynchronously using the next function
  implemented by the copy machine.
  @see @ref CPDLD "Copy Packet DLD" for more details.

  @subsection SNSRepairCMDLD-lspec-cm-stop Copy machine stop
  Once all the COBs(i.e. component objects) corresponding to the GOBs
  (i.e global file objects) belonging to the failure set are repaired by every
  replica in the cluster, it broadcasts DONE FOPs to all other replicas in the
  cluster. Once every replica receives DONE FOPs from every other replica, the
  operation is marked complete.

  @subsection SNSRepairCMDLD-lspec-thread Threading and Concurrency Model
  SNS Repair copy machine is implemented as a request handler service, thus it
  shares the request handler threading model and does not create its own
  threads. All the copy machine operations are performed in context of request
  handler threads.

  SNS Repair copy machine uses generic copy machine infrastructure, which
  implements copy machine state machine using generic Colibri state machine
  infrastructure. @ref State machine <!-- sm/sm.h -->

  @subsection SNSRepairCMDLD-lspec-numa NUMA optimizations
  N/A

  <hr>
  @section SNSRepairCMDLD-conformance Conformance
  @b i.sns.repair.trigger Various failures are reported through TRIGGER FOP,
  which create corresponding FOMs. FOMs invoke sns repair specific copy machine
  operations through generic copy machine interfaces which cause copy machine
  state transitions.

  @b i.sns.repair.buffer.acquire SNS Repair copy machine impllements its own
  buffer pool used to create copy pckets. The buffer pool is provisioned during
  the start of the repair operation.

  @b r.sns.repair.sliding.window Minimum sliding window size in the cluster is
  selected which is a broadcast by every replica using READY FOPs when repair
  operation starts. This enables copy machine to use resources efficiently.

  @b r.sns.repair.data.next SNS Repair copy machine implements a next function
  using cobfid_map and pdclust layout infrastructure to select the next data
  to be repaired from the failure set. This is done in GOB fid order as
  mentioned in the "HLD of SNS Repair".

  @b i.sns.repair.report.progress Progress is reported using sliding window and
  layout updates.
  @todo Layout updates will be implemented at later stages of sns repair.

  <hr>
  @section SNSRepairCMDLD-ut Unit tests

  <hr>
  @section SNSRepairCMDLD-st System tests
  N/A

  <hr>
  @section SNSRepairCMDLD-O Analysis
  N/A

  <hr>
  @section SNSRepairCMDLD-ref References
  @see @ref DLD-cm-ref
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
	SNS_BUF_COLOR = 1
};

extern struct c2_net_xprt c2_net_lnet_xprt;
extern struct c2_cm_type sns_repair_cmt;

int c2_sns_repair_cm_type_register(void)
{
	return c2_cm_type_register(&sns_repair_cmt);
}

void c2_sns_repair_cm_type_deregister(void)
{
	c2_cm_type_deregister(&sns_repair_cmt);
}

static struct c2_cm_cp *cm_cp_alloc(struct c2_cm *cm)
{
	struct c2_sns_repair_cp *rcp;
	struct c2_sns_repair_cm *rcm;
	struct c2_net_buffer    *buf;

	C2_PRE(c2_cm_invariant(cm));

	rcm = cm2sns(cm);
	buf = c2_net_buffer_pool_get(&rcm->rc_bp, SNS_BUF_COLOR);
	C2_ASSERT(buf != NULL);
	C2_ALLOC_PTR(rcp);
	if (rcp == NULL)
		return NULL;
	c2_cm_cp_init(&rcp->rc_base, &c2_sns_repair_cp_ops, &buf->nb_buffer);

	return &rcp->rc_base;	
}

static int cm_start(struct c2_cm *cm)
{
	struct c2_sns_repair_cm *rcm;
	struct c2_reqh          *reqh;
	struct c2_net_domain    *ndom;
	int                      rc;

	C2_ENTRY();

	rcm = cm2sns(cm);
	reqh = cm->cm_service.rs_reqh;
	ndom = c2_cs_net_domain_locate(c2_cs_ctx_get(reqh),
				       c2_net_lnet_xprt.nx_name);
	/*
	 * TODO: Send READY FOPs to other replicas in the cluster with sliding window
	 * size, calculate the minimum sliding window size within the cluster to start
	 * and create copy packets accordngly.
	 */
	rc = c2_net_buffer_pool_init(&rcm->rc_bp,
				     ndom,
				     C2_NET_BUFFER_POOL_THRESHOLD,
				     SNS_SEG_NR, SNS_SEG_SIZE,
				     SNS_BUF_COLOR, C2_0VEC_SHIFT);

	c2_cm_cp_create(cm);

	C2_LEAVE();
	return rc;
}

static int cm_config(struct c2_cm *cm)
{
	return 0;
}

static void cm_done(struct c2_cm *cm)
{

    /*
     * Broadcast DONE FOPs to other replicas
     * and wait for DONE FOPs from all the replicas.
     * Transition cm to IDLE state.
     */
}

static void cm_stop(struct c2_cm *cm)
{
	C2_PRE(cm != NULL);

    /*
     * Broadcast STOP FOPs to all other replicas and wait for
     * for STOP FOPs from all other replicas.
     * Transition CM to STOP state.
     */
}

static void cm_fini(struct c2_cm *cm)
{
	C2_ENTRY();
	C2_PRE(cm != NULL);
}

/** Copy machine operations. */
const struct c2_cm_ops cm_ops = {
	.cmo_start      = cm_start,
	.cmo_config     = cm_config,
	.cmo_cp_alloc   = cm_cp_alloc,
	.cmo_done       = cm_done,
	.cmo_stop       = cm_stop,
	.cmo_fini       = cm_fini
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
