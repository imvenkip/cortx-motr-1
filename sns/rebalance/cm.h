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
 * Original creation date: 11/28/2012
 */

#pragma once

#ifndef __COLIBRI_SNS_REBALANCE_CM_H__
#define __COLIBRI_SNS_REBALANCE_CM_H__

/**
  @page SNSRebalanceCMDLD-fspec SNS Rebalance copy machine.

  - @ref SNSRebalanceCMDLD-fspec-ds
  - @ref SNSRebalanceCMDLD-fspec-if
  - @ref SNSRebalanceCMDLD-fspec-usecases

  @section SNSRebalanceCMDLD-fspec Functional Specification
  SNS Rebalance copy machine

  @subsection SNSRebalanceCMDLD-fspec-ds Data Structures
  - c2_sns_rebalance_cm
    Represents sns rebalance copy machine, this embeds generic struct c2_cm and
    sns rebalance specific copy machine objects.

  @subsection SNSRebalanceCMDLD-fspec-if Interfaces
  - c2_sns_rebalance_cm_type_register
    Registers sns rebalance copy machine type and its corresponding request
    handler service type.
    @see c2_sns_init()

  - c2_sns_repair_cm_type_deregister
    De-registers sns rebalance copy machine type.
    @see c2_sns_fini()
 */

/**
  @defgroup SNSRebalanceCM SNS Rebalance copy machine
  @ingroup CM

  SNS-Rebalance copy machine is a replicated state machine similar to sns repair
  copy machine. SNS-Rebalance copy machine is triggered after completion of sns
  repair.

  @see The @ref SNSRebalanceCMDLD

  @{
*/

struct c2_sns_rebalance_cm {
	struct c2_cm               rc_base;
	/** SNS Repair data iterator. */
	struct c2_sns_repair_iter  rc_it;
	/** Buffer pool for outgoing copy packets. */
	struct c2_net_buffer_pool  rc_obp;
};

C2_INTERNAL int c2_sns_repair_cm_type_register(void);
C2_INTERNAL void c2_sns_repair_cm_type_deregister(void);

C2_INTERNAL struct c2_sns_rebalance_cm *cm2sns(struct c2_cm *cm);

#endif /* __COLIBRI_SNS_REBALANCE_CM_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
