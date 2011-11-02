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
 * This file contains kernel specific external interfaces of the LNet transport.
 */
#ifndef __COLIBRI_K_LNET_XPRT_H__
#define __COLIBRI_K_LNET_XPRT_H__

/**
   @page KLNet-fspec LNet Kernel Transport Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref KLNet-fspec-ds
   - @ref KLNet-fspec-sub

   @section KLNet-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   The transport is defined by the @a c2_net_lnet_xprt data structure.

   @section KLNet-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   There are two classes of externally accessible subroutine interfaces:
   - @em Consumer oriented interfaces
   - @em Core interfaces

   Consumer oriented interfaces are available to the application using the
   transport.
   They are exposed through the @ref LNetDFS "LNet Transport Interfaces".

   Core interfaces are used by the kernel transport operation subroutines
   themselves, and also by the user space LNet transport's device driver.
   They are exposed through
   @ref KLNetCore "LNet Kernel Transport Core Interfaces".

   Other interfaces are internal, and are documented in
   @ref KLNetIDFS "LNet Kernel Transport Internal Interfaces".

   @see @ref KLNet "LNet Kernel Transport Detailed Level Design"

 */

/* pick up common consumer subroutine declarations */
#include "net/lnet.h"

/* kernel only consumer subroutine declarations */
/**
   @addtogroup LNetDFS
   @{
 */

/**
   Sample kernel only consumer subroutine.
 */
extern void c2_net_lnet_consumer_sample_sub_kernel_only();


/**
   @}
*/
#endif /* __COLIBRI_K_LNET_XPRT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
