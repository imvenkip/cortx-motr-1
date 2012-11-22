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
#include "net/buffer_pool.h"
#include "layout/pdclust.h"

#include "sns/repair/iter.h"
#include "cob/ns_iter.h"

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

  - c2_sns_repair_iter
    Represents sns repair copy machine data iterator.

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

	/**
	 * Failure data received in trigger FOP.
	 * This is set when a TRIGGER FOP is received. For SNS Repair, this
	 * will be the failed container id.
	 * SNS Repair data iterator assumes this to be set before invoking
	 * c2_sns_repair_iter_next().
	 */
	uint64_t                   rc_fdata;

	/**
	 * Tunable file size parameter for testing purpose, set from the
	 * sns/repair/st/repair program.
	 */
	uint64_t                   rc_file_size;

	/** SNS Repair data iterator. */
	struct c2_sns_repair_iter  rc_it;

	/** Cob namespace iterator. */
	struct c2_cob_ns_iter      rc_cns_it;

	/*
	 * XXX Temporary location for layout domain required to build pdclust
	 * layout.
	 */
	struct c2_layout_domain    rc_lay_dom;

	/**
	 * Buffer pool for incoming copy packets, this is used by sliding
	 * window.
	 */
	struct c2_net_buffer_pool  rc_ibp;

	/** Buffer pool for outgoing copy packets. */
	struct c2_net_buffer_pool  rc_obp;

	/**
	 * Channel to wait upon before invoking c2_cm_stop() for the caller of
	 * c2_cm_start(). This channel is signalled from struct c2_cm_ops::
	 * cmo_complete() routine, which is invoked after all the aggregation
	 * groups are processed and struct c2_cm::cm_aggr_grps list is empty.
	 */
	struct c2_chan             rc_stop_wait;
};

C2_INTERNAL int c2_sns_repair_cm_type_register(void);
C2_INTERNAL void c2_sns_repair_cm_type_deregister(void);

C2_INTERNAL struct c2_net_buffer *c2_sns_repair_buffer_get(struct
							   c2_net_buffer_pool
							   *bp, size_t colour);
C2_INTERNAL void c2_sns_repair_buffer_put(struct c2_net_buffer_pool *bp,
					  struct c2_net_buffer *buf,
					  uint64_t colour);

C2_INTERNAL struct c2_sns_repair_cm *cm2sns(struct c2_cm *cm);

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
