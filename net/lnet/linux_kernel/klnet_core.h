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
#ifndef __COLIBRI_KLNET_CORE_H__
#define __COLIBRI_KLNET_CORE_H__

/**
   @defgroup KLNetCore LNet Transport Core Kernel Private Interfaces
   @ingroup LNetCore

   @{
*/

#include "lnet/include/lnet/types.h"
#include "lib/semaphore.h"

/**
   Kernel domain private data.
*/
struct c2_klnet_core_domain {
	/** Boolean indicating if the application is running in user space. */
	bool  klcd_user_space_app;
};

/**
   Kernel transfer machine private data.
*/
struct c2_klnet_core_transfer_mc {
	struct c2_lnet_core_domain *klctm_dom;

	/**
	   Semaphore to count the number of events in the queue.
	*/
	struct c2_semaphore    klctm_sem;

	/**
	   The circular buffer event queue. Each entry contains the
	   lcb_buffer_id of the buffer concerned.
	   The buffer may be maintained in user space.

	   CHANGE
	 */
	struct c2_cqueue           *klctm_cq;

	/** Handle of the LNet EQ associated with this transfer machine */
	lnet_handle_eq_t            lctm_eqh;
};


/**
   Kernel buffer private data.
*/
struct c2_klnet_core_buffer {
	struct c2_lnet_core_transfer_mc *klcb_tm;

	/**
	   The event array (receive buffers only) implemented using
	   a circular buffer as it can then be easily shared between the
	   single kernel producer and single user/kernel space consumer.
	   Each entry contains a struct c2_lnet_core_buffer_event data
	   structure.
	   The number of entries in the array are lcb_max_recv_msgs.
	   The buffer may be in user space.
	*/
	struct c2_cqueue                *klcb_events;


	/** The I/O vector. */
	struct lnet_kiov_t               klcb_kiov;

	/* other LNet handles as needed */
};

/**
   @}
*/

#endif /* __COLIBRI_KLNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
