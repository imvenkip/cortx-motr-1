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

#include "sns/repair/cm.h"
#include "reqh/reqh.h"
#include "lib/finject.h"

/**
  @page SNSRepairCMDLD SNS Repair copy machine DLD
  - @ref SNSRepairCMDLD-ovw
  - @ref SNSRepairCMDLD-def
  - @ref SNSRepairCMDLD-req
  - @ref SNSRepairCMDLD-depends
  - @ref SNSRepairCMDLD-highlights
  - @subpage SNSRepairCMDLD-fspec
     - @ref SNSRepairCMDLD-lspec
     - @ref SNSRepairCMDLD-lspec-cm-start
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
  startup, it can also be started later.

  <hr>
  @section SNSRepairCMDLD-def Definitions
  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1Yz25F3GjgQVXzvM1sdlGQvVDSUu-v7FhdUvFhiY_vwM/edit#heading=h.4493e2a5a920

  Refer to <a ref https://docs.google.com/a/xyratex.com/document/d/
  1ZlkjayQoXVm-prMxTkzxb1XncB6HU19I19kwrV-8eQc/edit#heading=h.8c396c004f1f>


  <hr>
  @section SNSRepairCMDLD-req Requirements
  - @b r.sns.repair.trigger SNS repair copy machine should respond to triggers
    caused due to various kinds of failures as mentioned in the HLD of SNS
    Repair.

  - @b r.sns.repair.buffer.acquire The implementation should efficiently provide
    buffers for the repair operation without any deadlock.

  - @b r.sns.repair.sliding.window The implementation should efficiently use
    various copy machine resources using sliding window during copy machine
    operation, e.g. memory, cpu, &c.

  - @b r.sns.repair.data.next The implementation should efficiently select next
    data to be processed without causing any deadlock or bottle neck.

  - @b r.sns.repair.report.progress The implementation should efficiently report
    overall progress of data re-structuring and update corresponding layout
    information for re-structured objects.

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
  - SNS Repair buffer pool provisioning is done when failure happens. 
  - SNS Repair defines its specific aggregation group data structure which
    embeds generic aggregation group.

  <hr>
  @section SNSRepairCMDLD-lspec Logical specification
  - @ref SNSRepairCMDLD-lspec-cm-init
  - @ref SNSRepairCMDLD-lspec-cm-start
  - @ref SNSRepairCMDLD-lspec-cm-stop

  @subsection SNSRepairCMDLD-lspec-comps Component overview
  The focus of sns repair copy machine is on re-structuring the data
  efficiently. The re-structuring operation is split into various copy packet
  phases.

  @subsection SNSRepairCMDLD-lspec-cm-init Copy machine startup
  SNS Repair defines its following specific data structure to represent a
  copy machine.
  @code
  struct c2_sns_repair_cm {
          struct c2_cm               rc_base;
          struct c2_net_buffer_pool  rc_pool; 
  };
  @endcode

  SNS Repair implements its specific operations for
  struct c2_reqh_service_type_ops, struct c2_reqh_service_ops, and
  struct c2_cm_ops.

   

  Note: Please refer reqh/reqh_service.c for further details on request handler
  service.

  Note:
  - Copy machine start operation can block as it fetches configuration
    information from configuration service.

  @subsection SNSRepairCMDLD-lspec-cm-stop Copy machine stop
  SNS Repair copy machine is stopped when its corresponding service is
  stopped. Again, the service stop operation invokes generic c2_cm_stop(),
  which further invokes sns repair copy machine specific stop operation.

  @subsection SNSRepairCMDLD-lspec-thread Threading and Concurrency Model
  SNS Repair copy machine is implemented as a request handler service, thus
  it shares the request handler threading model and does not create its own
  threads. Thus all the copy machine operations are performed in context of
  request handler threads.

  SNS Repair copy machine uses generic copy machine infrastructure, which
  implements copy machine state machine using generic Colibri state machine
  infrastructure.
  @ref State machine <!-- sm/sm.h -->

  @subsection SNSRepairCMDLD-lspec-numa NUMA optimizations
  N/A

  <hr>
  @section SNSRepairCMDLD-conformance Conformance
  @b i.sns.repair.trigger Various triggers are reported through FOPs, which create
  corresponding FOMs. FOMs invoke sns repair specific copy machine operations
  through generic copy machine interfaces which cause copy machine state
  transitions.

  @b i.sns.repair.report.progress Progress is reported using sliding window and
  layout updates.
  @todo Layout updates will be implemented at later stages of sns repair.

  @b i.sns.repair.buffer.acquire SNS Repair copy machine impllements its own
  buffer pool used to create copy pckets. The buffers are allocated in 

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
  data efficiently in event of a failure. It uses GOB (global file object) and
  COB (component object) infrastructure and parity de-clustering layout.

  SNS Repair copy machine is typically started during colibri setup, but can
  also be started later. SNS Repair copy machine mainly uses reqh and generic
  copy machine framework, and thus implements their corresponding operations.

  @{
*/

/** Copy machine operations.*/
static int cm_start(struct c2_cm *cm);
static int cm_config(struct c2_cm *cm);
static void cm_done(struct c2_cm *cm);
static void cm_stop(struct c2_cm *cm);
static void cm_fini(struct c2_cm *cm);

const struct c2_cm_ops cm_ops = {
	.cmo_start      = cm_start,
	.cmo_config     = cm_config,
	.cmo_done       = cm_done,
	.cmo_stop       = cm_stop,
	.cmo_fini       = cm_fini
};

extern struct c2_cm_type sns_repair_cmt;

int c2_sns_repair_cm_type_register(void)
{
	return c2_cm_type_register(&sns_repair_cmt);
}

void c2_sns_repair_cm_type_deregister(void)
{
	c2_cm_type_deregister(&sns_repair_cmt);
}

static int cm_start(struct c2_cm *cm)
{
	int rc = 0;

	C2_ENTRY();

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
