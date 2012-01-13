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
#ifndef __COLIBRI_NET_LNET_H__
#define __COLIBRI_NET_LNET_H__

/**
   @page LNetDLD-fspec LNet Transport Functional Specification

   - @ref LNetDLD-fspec-ds
   - @ref LNetDLD-fspec-sub

   @section LNetDLD-fspec-ds Data Structures
   The module implements a network transport protocol using the data structures
   defined by @ref net/net.h.

   The LNet transport is defined by the ::c2_net_lnet_xprt data structure.
   The address of this variable should be provided to the c2_net_domain_init()
   subroutine.


   @section LNetDLD-fspec-sub Subroutines
   New subroutine provided:
   - c2_net_lnet_ep_addr_net_cmp()
     Compare the network portion of two LNet transport end point addresses.
     It is intended for use by the Request Handler setup logic.

   The use of this subroutine is not mandatory.

   @see @ref net "Networking"                     <!-- net/net.h -->
   @see @ref LNetDLD "LNet Transport DLD"         <!-- net/lnet/lnet_main.c -->
   @see @ref LNetDFS "LNet Transport"   <!-- below -->

 */

/**
   @defgroup LNetDFS LNet Transport
   @ingroup net

   The external interfaces of the LNet transport are obtained by
   including the file @ref net/lnet/lnet.h.

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
	/** Max LNet ep addr length
	    @todo Determine exact value for a 4-tuple LNet EP addr
	*/
	C2_NET_LNET_XEP_ADDR_LEN = 80,
};

/**
   Subroutine compares the network portions of two LNet end point address
   strings.
   @retval int Return value like strcmp().
 */
int c2_net_lnet_ep_addr_net_cmp(const char *addr1, const char *addr2);

/**
   @} LNetDFS end group
*/

#endif /* __COLIBRI_NET_LNET_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
