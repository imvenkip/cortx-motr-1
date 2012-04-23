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
#ifndef __COLIBRI_NET_ULNET_CORE_H__
#define __COLIBRI_NET_ULNET_CORE_H__

/**
   @defgroup ULNetCore LNet Transport Core Userspace Private Interface
   @ingroup LNetCore

   @todo This section will be filled in by the (net, lnet-user, DLD) task.

   @{
*/

/**
   Userspace domain private data.
   This structure is pointed to by nlx_core_domain::lcd_upvt.
 */
struct nlx_ucore_domain {
	/** File descriptor to the kernel device */
	int      ucd_fd;
};

/**
   Userspace transfer machine private data.
   This structure is pointed to by nlx_core_transfer_mc::lctm_upvt.
*/
struct nlx_ucore_transfer_mc {

};

/**
   Userspace buffer private data.
   This structure is pointed to by nlx_core_buffer::lcb_upvt.
*/
struct nlx_ucore_buffer {

};

/**
   @}
*/

#endif /* __COLIBRI_NET_ULNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
