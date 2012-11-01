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

#include "cm/cm.h"
#include "ioservice/cobfid_map.h"
#include "net/buffer_pool.h"
#include "layout/pdclust.h"

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
  Response: SNS repair copy machine service is started and copy machine is
            initialised.
 */

/**
  @defgroup SNSRepairCM SNS Repair copy machine
  @ingroup CM

  SNS-Repair copy machine is a replicated state machine, which performs data
  restructuring and handles device, container, node, &c failures.
  @see The @ref SNSRepairCMDLD

  @{
*/

struct c2_sns_repair_cm {
	struct c2_cm		   rc_base;
	/** Failure data received in trigger FOP. */
	uint64_t                   rc_fdata;
	struct c2_cobfid_map      *rc_cfm;
	struct c2_cobfid_map_iter  rc_cfm_it;
	/**
	 * @todo Temporary location for server side pdclust layout instance, until
	 * the attr_set() and attr_get() for a GOB is implemented.
	 */
	struct c2_pdclust_layout   rc_pl;
	struct c2_layout_domain    rc_lay_dom;
	/**
	 * Buffer pool for incoming copy packets, this is used by sliding
	 * window.
	 */
	struct c2_net_buffer_pool  rc_ibp;
	/** Buffer pool for outgoing copy packets. */
	struct c2_net_buffer_pool  rc_obp;
};

int c2_sns_repair_cm_type_register(void);
void c2_sns_repair_cm_type_deregister(void);

struct c2_sns_repair_cm *cm2sns(struct c2_cm *cm);

/** @} SNSRepairCM */
#endif /* __COLIBRI_SNS_REPAIR_CM_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
