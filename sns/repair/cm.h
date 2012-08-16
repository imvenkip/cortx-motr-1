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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 09/08/2012
 */

#pragma once

#ifndef __COLIBRI_SNS_REPAIR_CM_H__
#define __COLIBRI_SNS_REPAIR_CM_H__

/**
  @page SNSRepairCMDLD-fspec SNS Repair copy machine functional specification

  - @ref SNSRepairCMDLD-fspec-ds
  - @ref SNSRepairCMDLD-fspec-if
  - @ref SNSRepairCMDLD-fspec-usecases

  @section SNSRepairCMDLD-fspec Functional Specification
  SNS Repair copy machine

  @subsection SNSRepairCMDLD-fspec-ds Data Structures
  - c2_sns_repair_cm
    Represents sns repair copy machine, this embeds generic struct c2_cm and
    sns specific copy machine objects.

  @subsection SNSRepairCMDLD-fspec-if Interfaces
  - c2_sns_repair_cm_type_register
    Registers sns repair copy machine type and its corresponding request
    handler service type.
    @see c2_sns_init()

  - c2_sns_repair_cm_type_deregister
    De-registers sns repair copy machine type and its corresponding request
    handler service type.
    @see c2_sns_fini()

  @subsection SNSRepairCMDLD-fspec-usecases Recipes
  Test: Start sns repair copy machine service using colibri_setup
  Response: SNS repair copy machine service is started.
 */

/**
  @defgroup SNSRepairCM SNS Repair copy machine
  @ingroup CM
  
  SNS-Repair copy machine is a replicated state machine, which performs data
  re-structuring and handles device, container, node, &c failures.
  @see The @ref SNSRepairCMDLD

  @{
*/

#include "cm/cm.h"

struct c2_sns_repair_cm {
	struct c2_cm		  rc_base;
	struct c2_net_buffer_pool rc_bp;
};

int c2_sns_repair_cm_type_register(void);
void c2_sns_repair_cm_type_deregister(void);

static inline struct c2_sns_repair_cm *cm2sns(struct c2_cm *cm)
{
	return container_of(cm, struct c2_sns_repair_cm, rc_base);
}

/** @} SNSRepairCM */
#endif /* __COLIBRI_SNS_REPAIR_CM_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

