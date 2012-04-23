/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 11/01/2011
 *
 */
#ifndef __COLIBRI_NET_KLNET_CORE_H__
#define __COLIBRI_NET_KLNET_CORE_H__

/**
   @defgroup KLNetCore LNet Transport Core Kernel Private Interface
   @ingroup LNetCore

   @{
*/

#include "lib/semaphore.h"
#include "lib/tlist.h"
#include "lnet/include/lnet/types.h"
#include "net/lnet/lnet_core.h"

#include <linux/spinlock.h>

enum {
	C2_NET_LNET_KCORE_TM_MAGIC  = 0x4b436f7265544dULL,   /* KCoreTM */
	C2_NET_LNET_KCORE_BUF_MAGIC = 0x4b436f7265427566ULL, /* KCoreBuf */
};

/**
   Kernel transfer machine private data.
   This structure is pointed to by c2_lnet_core_transfer_mc::lctm_kpvt.
*/
struct nlx_kcore_transfer_mc {
	uint64_t                         ktm_magic;

	/**
	   Kernel pointer to the shared memory TM structure.
	 */
	struct c2_lnet_core_transfer_mc *ktm_tm;

	/** Transfer machine linkage */
	struct c2_tlink                  ktm_tm_linkage;

	/**
	   Match bit counter. Range [1,C2_NET_LNET_MATCH_BIT_MAX].
	 */
	uint64_t                         ktm_mb_counter;

	/**
	   Spin lock to serialize access to the buffer event queue
	   from the LNet callback subroutine.
	 */
	spinlock_t                       ktm_bevq_lock;

	/**
	   This semaphore increments with each LNet event added.

	*/
	struct c2_semaphore              ktm_sem;

	/** Handle of the LNet EQ associated with this transfer machine */
	lnet_handle_eq_t                 ktm_eqh;

	/** ME handle associated with the receive buffer queue */
	lnet_handle_me_t                 ktm_meh;

};


/**
   Kernel buffer private data.
   This structure is pointed to by c2_lnet_core_buffer::lcb_kpvt.
*/
struct nlx_kcore_buffer {
	uint64_t                      kb_magic;

	/** Mimumum space remaining for re-use of the receive buffer.
	    The value is set from c2_net_buffer::nb_min_receive_size.
	 */
	c2_bcount_t                   kb_min_recv_size;

	/** Maximum number of messages that may be received in the buffer.
	    The value is set from c2_net_buffer::nb_max_receive_msgs.
	 */
	uint32_t                      kb_max_recv_msgs;

	/** Pointer to kernel core TM data. */
	struct nlx_kcore_transfer_mc *kb_ktm;

	/** The I/O vector. */
	struct lnet_kiov_t            kb_kiov;

	/** MD handle */
	lnet_handle_md_t              kb_mdh;
};

/**
   @}
*/

#endif /* __COLIBRI_NET_KLNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
