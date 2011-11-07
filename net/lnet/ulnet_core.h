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
#ifndef __COLIBRI_ULNET_CORE_H__
#define __COLIBRI_ULNET_CORE_H__

/**
   @defgroup ULNetCore LNet Transport Core Userspace Private Interfaces
   @ingroup LNetCore

   @{
*/

#include "lib/circular_queue.h"

/**
   Userspace domain private data.
 */
struct c2_ulnet_core_domain {
	/** Size of the buffer private data */
	size_t                ulcd_buf_pvt_size;

	/** Size of the transfer machine private data */
	size_t                ulcd_tm_pvt_size;

	/** Maximum messages in a single receive buffer */
	uint32_t              ulcd_max_recv_msgs;

	/** File descriptor to the kernel device */
	int                   ulcd_fd;
};

/**
   Userspace transfer machine private data.
*/
struct c2_ulnet_core_transfer_mc {
	struct c2_lnet_core_domain *ulctm_dom;

	/**
	   The circular buffer event queue. Each entry contains the
	   lcb_buffer_id of the buffer concerned.
	 */
	struct c2_circular_queue  *ulctm_cq;
};

/**
   Userspace buffer private data.
*/
struct c2_ulnet_core_buffer {
	struct c2_lnet_core_transfer_mc *ulcb_tm;

	/**
	   The event array (receive buffers only) implemented using
	   a circular buffer as it can then be easily shared between the
	   single kernel producer and single user/kernel space consumer.
	   Each entry contains a struct c2_lnet_core_buffer_event data
	   structure.
	   The number of entries in the array are lcb_max_recv_msgs.
	*/
	struct c2_circular_queue *ulcb_events;

};

/**
   @}
*/

#endif /* __COLIBRI_ULNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
