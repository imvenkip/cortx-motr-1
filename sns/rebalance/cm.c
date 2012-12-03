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
 * Original creation date: 11/30/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/**
  @page SNSRebalanceCMDLD SNS Rebalance copy machine DLD
  - @ref SNSRebalanceCMDLD-ovw
  - @ref SNSRebalanceCMDLD-def
  - @ref SNSRebalanceCMDLD-req
  - @ref SNSRebalanceCMDLD-depends
  - @ref SNSRebalanceCMDLD-highlights
  - @subpage SNSRebalanceCMDLD-fspec
  - @ref SNSRebalanceCMDLD-lspec
     - @ref SNSRebalanceCMDLD-lspec-cm-setup
     - @ref SNSRebalanceCMDLD-lspec-cm-start
        - @ref SNSRebalanceCMDLD-lspec-cm-active
     - @ref SNSRebalanceCMDLD-lspec-cm-stop
  - @ref SNSRebalanceCMDLD-conformance
  - @ref SNSRebalanceCMDLD-ut
  - @ref SNSRebalanceCMDLD-st
  - @ref SNSRebalanceCMDLD-O
  - @ref SNSRebalanceCMDLD-ref

  <hr>
  @section SNSRebalanceCMDLD-ovw Overview
  The focus of SNS Rebalance copy machine is to efficiently copy the repaired
  data from the spare units to the target unit in spare device using layout.
  SNS rebalance copy machine is triggered after completion of SNS repair.

  <hr>
  @section SNSRebalanceCMDLD-def Definitions
  Please refer to "Definitions" section in "HLD of copy machine and agents".

  <hr>
  @section SNSRebalanceCMDLD-req Requirements
  - @b r.sns.rebalance.iter SNS Rebalance copy machine should iterate only
    the repaired parity groups and copy the data from corresponding spare units
    to the target unit on the new device.
  - @b r.sns.rebalance.layout SNS Rebalance copy machine should use the same
    layout as used by the sns repair to map a spare unit to the target unit on
    new device.

  <hr>
  @section SNSRebalanceCMDLD-depends Dependencies
  - @b r.sns.rebalance.iter It should possible to use SNS Repair data iterator
    infrastructure for SNS Rebalance operation. Thus the interfaces should be
    generic enough to be used by both the copy machines.
  - @b r.sns.rebalance.layout It should be possible to map a spare unit to a
    target unit on the new device using layout.

  <hr>
  @section SNSRebalanceCMDLD-highlights Design highlights
  - SNS rebalance copy machine is implemented as a separate copy machine type.
  - SNS rebalance copy machine uses generic c2_reqh_service infrastructure to
    implement copy machine service.
  - SNS rebalance copy machine uses the generic copy machine and copy packet
    infrastructure and maintains its own buffer pool.
  - SNS rebalance copy machine uses existing SNS Repair data iterator to iterate
    through spare units. SNS Rebalance copy machine maintains a separate
    instance of SNS Repair data iterator and thus configures it separately.
    @todo To add a bit mask to struct c2_sns_repair_iter, representing the copy
    machine type (i.e. SNS Repair or SNS Rebalance).
  - Each used spare unit corresponds to exactly one (data or parity) unit on
    the lost device, thus SNS rebalance copy machine uses layout to calculate
    the target unit offset in the new device corresponding to the spare unit
    in the parity group.

  <hr>
  @section SNSRebalanceCMDLD-lspec Logical specification
  - @ref SNSRebalanceCMDLD-lspec-cm-setup
  - @ref SNSRebalanceCMDLD-lspec-cm-start
     - @ref SNSRebalanceCMDLD-lspec-cm-active
  - @ref SNSRebalanceCMDLD-lspec-cm-stop

  @subsection SNSRebalanceCMDLD-lspec-cm-setup Copy machine setup
  SNS Rebalance copy machine is allocated and initialised by its corresponding
  c2_reqh_service. SNS Rebalance copy machine setup initialises copy machine
  type specific resources, viz. buffer pool.
  Once the setup completes successfully, copy machine transitions to
  C2_CMS_IDLE state, and wait for a start event.

  @subsection SNSRebalanceCMDLD-lspec-cm-startup Copy machine startup
  Once SNS Repair is complete, SNS Rebalance operation starts. This also means
  there exist a new device to which the repaired data is to be copied. Before
  the operation starts, the buffer pool is provisioned with N number of empty
  buffers (value of N can vary depending on the performance). Once all the
  startup activities complete successfully, copy machine transitions to
  C2_CMS_ACTIVE state.

  @subsubsection SNSRebalanceCMDLD-lspec-cm-active Copy machine processing
  As mentioned earlier, SNS Rebalance copy machine uses generic copy packet
  infrastructure for data transfer. After buffer pool provisioning, SNS
  Rebalance copy machine uses SNS Repair data iterator to iterate over repaired
  parity groups. Copy machine on every node reads local spare units from the
  repaired parity groups and creates corresponding copy packets. Creation of
  copy packets and the progress of rebalance operation depends on the
  availability of empty buffers in the buffer pool (same as that in case of SNS
  Repair).
  Most of the SNS Repair data iterator algorithm remains the same except few
  changes, where the iterator skips the data units and instead reads only from
  the spare units. Following pseudo code illustrates the data iterator for SNS
  Rebalance copy machine,

  @code
  - for each GOB G in cob name-space (in global fid order)
    - fetch layout L for G
    // proceed in parity group order
    - for each parity group S until EOF G
      - map parity group S to the COB list
      // determine if parity group S is repaired
      - if no COB.containerid is in the failure set continue to the next group
      // group's spare unit has to be read
      - Calculate spare unit U in S (N + K < U <= N + 2K)
      - map (S, U) -> (COB, F) by L
      - if COB is local and COB.containerid does not belong to the failure set
        - fetch frame F of COB
        - create copy packet
  @endcode

  @subsection SNSRebalanceCMDLD-lspec-cm-stop Copy machine stop
  After copying all the repaired data from spare units to the new device, the
  SNS Rebalance is marked as complete. On successful completion, the copy
  machine transitions back to C2_CMS_IDLE state and wait for the further events.

  @section SNSRebalanceCMDLD-conformance Conformance
  - @b i.sns.rebalance.iter SNS Rebalance uses existing SNS Repair data iterator
    with slight modifications to iterate over repaired parity groups and process
    only the spare unit/s from a parity group.
  - @b i.sns.rebalance.layout An interface is added to SNS Repair data iterator
    to map a spare unit to a failed data/parity unit from a group using the
    layout.

  <hr>
  @section SNSRebalanceCMDLD-ut
  N/A

  <hr>
  @section SNSRebalanceCMDLD-st
  N/A

  <hr>
  @section SNSRebalanceCMDLD-O
  N/A

  <hr>
  @section SNSRebalanceCMDLD-ref

*/

/**
  @addtogroup SNSRebalanceCM

  SNS Rebalance copy machine implements a copy machine in-order to re-structure
  data efficiently in an event of a failure. It uses GOB (global file object)
  and COB (component object) infrastructure with parity de-clustering layout.

  @{
*/



/** @} SNSRebalanceCM */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
