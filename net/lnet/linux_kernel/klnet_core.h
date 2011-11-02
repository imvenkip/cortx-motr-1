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
 * This file contains private interfaces of the LNet kernel transport.
 */
#ifndef __COLIBRI_K_LNET_CORE_H__
#define __COLIBRI_K_LNET_CORE_H__

#include "net/lnet/linux_kernel/klnet_xprt.h"

/**
   @defgroup KLNetCore LNet Kernel Transport Core Interfaces
   @ingroup LNetDFS

   Internal interfaces used by the LNet kernel transport operation
   subroutines and the user space LNet transport's device driver.  These
   interfaces are available through @ref net/lnet/linux_kernel/klnet_core.h.

   @see @ref LNetDFS "LNet Transport Interfaces"
   @see @ref KLNetIDFS "LNet Kernel Transport Internal Interfaces"
   @see @ref KLNet "LNet Kernel Transport Detailed Level Design"

   @{
 */

/**
   Core subroutine.
 */
extern void c2_net_lnet_core_sample_sub();


/**
   @}
*/

#endif /* __COLIBRI_K_LNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
