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
 */
#ifndef __COLIBRI_LNET_H__
#define __COLIBRI_LNET_H__

/**
   @page LNetDLD-fspec LNet Transport Functional Specification
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

   - @ref LNetDLD-fspec-ds
   - @ref LNetDLD-fspec-sub

   @section LNetDLD-fspec-ds Data Structures
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and <i>brief</i> description of the
   major externally visible data structures defined by this component.  No
   details of the data structure are required here, just the salient
   points.</i>

   The module implements a network transport protocol using the data structures
   defined by @ref net/net.h.

   The LNet transport is defined by the @c c2_net_lnet_xprt data structure.

   @section LNetDLD-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   Routines are provided for the following:
   - Compare the network portion of two end point addresses.
   - Set domain specific run time parameters.

   @see @ref LNetDLD "LNet Transport DLD"

 */

/**
   @defgroup LNetDFS LNet Transport Interface
   @ingroup net

   The external interfaces of the LNet transport are obtained by
   including the file @ref net/lnet.h.

   @see @ref LNetDLD "LNet Transport DLD"

   @{
*/
#include "net/net.h"

/**
   The LNet transport is used by specifying this data structure to the
   c2_net_domain_init() subroutine.
 */
extern struct c2_net_xprt c2_net_lnet_xprt;

enum {
	C2_NET_LNET_XEP_ADDR_LEN = 80, /**< @todo Max addr length?, 4-tuple */
};

/**
   Subroutine to compare the network portions of two LNet end point address
   strings.
   @retval true The network components are the same.
   @retval false The network components are different.
 */
extern bool c2_net_lnet_ep_addr_net_compare(const char *addr1,
					    const char *addr2);



/**
   @} LNetDFS end group
*/

#endif /* __COLIBRI_LNET_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

