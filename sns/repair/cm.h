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

#ifndef __COLIBRI_SNS_REPAIR_CM_H__
#define __COLIBRI_SNS_REPAIR_CM_H__

/**
  @page DLD-snsrepair-fspec SNS-Repair copy machine functional specification

  - @ref DLD-snsrepair-fspec-ds
  - @ref DLD-snsrepair-fspec-if
  - @ref DLD-snsrepair-fspec-usecases

  @section DLD-snsrepair-fspec Functional Specification
  SNS Repair copy machine is implemented using the data structures and
  subroutines defined in reqh/reqh_service.h and cm/cm.h.

  @subsection DLD-snsrepair-fspec-ds Data Structures
  - c2_sns_repair_cm
    Represents sns repair copy machine, this embeds generic struct c2_cm and
    sns specific copy machine objects.

  - c2_sns_repair_cm_aggr_group
    Represents sns repair specific aggregation group, this embeds generic
    struct c2_cm_aggr_group and sns repair specific information.

  @subsection DLD-snsrepair-fspec-if Interfaces
  - c2_sns_repair_cm_type_register
    Registers sns repair copy machine type and its corresponding request
    handler service type.
    @see c2_sns_init()

  - c2_sns_repair_cm_type_deregister
    De-registers sns repair copy machine type and its corresponding request
    handler service type.
    @see c2_sns_fini()

  @subsection DLD-snsrepair-fspec-usecases Recipes
  Test: Start sns repair copy machine service using colibri_setup
  Response: SNS repair copy machine service is started and creates
            corresponding agents.
 */

/**
  @defgroup snsrepair SNS Repair copy machine
  SNS-Repair copy machine is a replicated state machine, which performs data
  re-structuring and handles device, container, node, &c failures.
  @see The @ref DLD-snsrepair

  @{
*/

#include "cm/cm.h"

struct c2_net_buffer_pool;

struct c2_sns_repair_cm {
	struct c2_cm               sr_base;
	/** Buffer pool for copy packet header.*/
        struct c2_net_buffer_pool *sr_cp;
	/**
	 * Incoming buffer pools for copy packet data. Used by network-in and
	 * storage-out agents.
	 */
        struct c2_net_buffer_pool *sr_in;
	/**
	 * Outgoing buffer pools for copy packet data. Used by storage-in and
	 * network-out agents.
	 */
        struct c2_net_buffer_pool *sr_out;
};

int c2_sns_repair_cm_type_register(void);
void c2_sns_repair_cm_type_deregister(void);

/** @} snsrepair */
/* __COLIBRI_SNS_REPAIR_H__ */

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
