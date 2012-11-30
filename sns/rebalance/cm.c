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
  @section SNSRebalanceCMDLD-req
  - @b r.sns.rebalance.copy SNS Rebalance copy machine should iterate only
    the repaired parity groups and copy the data from corresponding spare units
    to the target unit on the new device.
  - @b r.sns.rebalance.layout SNS Rebalance copy machine should use the same
    layout as used by the sns repair to map a spare unit to the target unit on
    new device.

  <hr>
  @section SNSRebalanceCMDLD-depends Dependencies
  - @b r.sns.rebalance.layout It should be possible to map a spare unit to a
    target unit on the new device using layout.

  <hr>
  @section SNSRebalanceCMDLD-highlights Design highlights
  - SNS rebalance copy machine is implemented as a separate copy machine type.
  - SNS rebalance copy machine uses generic c2_reqh_service infrastructure to
    implement copy machine service.
  - SNS rebalance copy machine uses existing sns repair iterator to iterate
    through spare units.
  - Each used spare unit corresponds to exactly one (data or parity) unit on
    the lost device, thus SNS rebalance copy machine uses layout to calculate
    the target unit offset in the new device corresponding to the spare unit
    in the parity group.

  <hr>
  @section SNSRebalanceCMDLD-lspec Logical specification
  - @ref SNSRebalanceCMDLD-lspec-cm-setup
  - @ref SNSRebalanceCMDLD-lspec-cm-start
  - @ref SNSRebalanceCMDLD-lspec-cm-stop

  @subsection SNSRebalanceCMDLD-lspec-cm-setup Copy machine setup

  @subsection SNSRebalanceCMDLD-lspec-cm-startup Copy machine starup

  @subsection SNSRebalanceCMDLD-lspec-cm-stop Copy machine stop

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
