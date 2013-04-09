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

#ifndef __MERO_SNS_CM_H__
#define __MERO_SNS_CM_H__

#include "cm/cm.h"
#include "net/buffer_pool.h"
#include "layout/pdclust.h"

#include "sns/cm/iter.h"

/**
  @page SNSCMDLD-fspec SNS copy machine functional specification

  - @ref SNSCMDLD-fspec-ds
  - @ref SNSCMDLD-fspec-if
  - @ref SNSCMDLD-fspec-usecases-repair
  - @ref SNSCMDLD-fspec-usecases-rebalance

  @section SNSCMDLD-fspec Functional Specification
  SNS copy machine provides infrastructure to perform tasks like repair and
  re-balance using parity de-clustering layout.

  @subsection SNSCMDLD-fspec-ds Data Structures
  - m0_sns_cm_cm
    Represents sns copy machine, this embeds generic struct m0_cm and sns
    specific copy machine objects.

  - m0_sns_cm_iter
    Represents sns copy machine data iterator.

  @subsection SNSCMDLD-fspec-if Interfaces
  - m0_sns_cm_cm_type_register
    Registers sns copy machine type and its corresponding request handler
    service type.
    @see m0_sns_init()

  - m0_sns_cm_cm_type_deregister
    De-registers sns copy machine type and its corresponding request handler
    service type.
    @see m0_sns_fini()

  @subsection SNSCMDLD-fspec-usecases-repair SNS repair recipes
  Test 01: Start sns copy machine service using mero_setup
  Response: SNS copy machine service is started and copy machine is
            initialised.
  Test 02: Configure sns copy machine for repair, create COBs and simulate a
           COB failure.
  Response: The parity groups on the survived COBs should be iterated and
            repair completes successfully.

  @subsection SNSCMDLD-fspec-usecases-rebalance SNS re-balance recipes
  Test 01: Start sns copy machine for repair, once repair is completeand copy
           is stopped, re-start the same copy machine to perform re-balance
           operation.
  Response: Re-balance should complete successfully.
 */

/**
  @defgroup SNSCM SNS copy machine
  @ingroup CM

  SNS copy machine is a replicated state machine, which performs data
  restructuring and handles device, container, node, &c failures.
  @see The @ref SNSCMDLD

  @{
*/

/**
 * Operation that sns copy machine is carrying out.
 */
enum m0_sns_cm_op {
	SNS_REPAIR = 1 << 1,
	SNS_REBALANCE = 1 << 2
};

struct m0_sns_cm_buf_pool {
	struct m0_net_buffer_pool sb_bp;
	/**
	 * Channel to wait for buffers in buffer pool to be released.
	 * This channel is signalled when any acquired buffer belonging to
	 * buffer pool is released back to the buffer pool
	 */
	struct m0_chan            sb_wait;
	struct m0_mutex           sb_wait_mutex;
};

struct m0_sns_cm {
	struct m0_cm		   sc_base;

	/** Operation that sns copy machine is going to execute. */
	enum m0_sns_cm_op          sc_op;

	uint64_t                   sc_failures_nr;

	/** SNS copy machine data iterator. */
	struct m0_sns_cm_iter      sc_it;

	/**
	 * Buffer pool for incoming copy packets, this is used by sliding
	 * window.
	 */
	struct m0_sns_cm_buf_pool  sc_ibp;

	/** Buffer pool for outgoing copy packets. */
	struct m0_sns_cm_buf_pool  sc_obp;

	/**
	 * Channel to wait upon before invoking m0_cm_stop() for the caller of
	 * m0_cm_start(). This channel is signalled from struct m0_cm_ops::
	 * cmo_complete() routine, which is invoked after all the aggregation
	 * groups are processed and struct m0_cm::cm_aggr_grps list is empty.
	 */
	struct m0_chan             sc_stop_wait;
	struct m0_mutex            sc_stop_wait_mutex;
};

M0_INTERNAL int m0_sns_cm_type_register(void);
M0_INTERNAL void m0_sns_cm_type_deregister(void);

M0_INTERNAL struct m0_net_buffer *m0_sns_cm_buffer_get(struct m0_net_buffer_pool
						       *bp, size_t colour);
M0_INTERNAL void m0_sns_cm_buffer_put(struct m0_net_buffer_pool *bp,
					  struct m0_net_buffer *buf,
					  uint64_t colour);

M0_INTERNAL int m0_sns_cm_buf_attach(struct m0_cm_cp *cp,
				     struct m0_net_buffer_pool *bp);

M0_INTERNAL struct m0_sns_cm *cm2sns(struct m0_cm *cm);

M0_INTERNAL uint64_t m0_sns_cm_data_seg_nr(struct m0_sns_cm *scm);

M0_INTERNAL void m0_sns_cm_buf_available(struct m0_net_buffer_pool *pool);

/** @} SNSCM */
#endif /* __MERO_SNS_CM_CM_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
