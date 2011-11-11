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
   The address of this variable should be provided to the c2_net_domain_init()
   subroutine.

   The module adds two additional fields to the c2_net_buffer structure:
   @code
   struct c2_net_buffer {
        ...
	c2_bcount_t   nb_min_receive_size;
	uint32_t      nb_max_receive_msgs;
   };
   @endcode
   These fields are required to be set to non-zero values in receive buffers,
   and control the reception of multiple messages into a single receive buffer.

   The module adds additional operations to the @c c2_net_xo_ops structure:
   @code
   struct c2_net_xo_ops {
        ...
        int  (*xo_tm_confine)(struct c2_net_transfer_mc *tm,
	                      const struct c2_bitmap *processors);
   };
   @endcode
   This is not directly visible to the consumer of the @ref net/net.h API, but
   it enables the use of the new c2_net_tm_confine() subroutine with the LNet
   transport.

   @section LNetDLD-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   New subroutines provided:
   - c2_net_tm_confine() Set processor affinity for transfer machine threads
   - c2_net_lnet_ep_addr_net_compare()
     Compare the network portion of two LNet transport end point addresses.
   - c2_net_lnet_tm_set_num_threads()
     Sets the number of threads to use in a transfer machine.

   The use of these subroutines is not mandatory.

   @todo LNet xo set domain specific parameters


   @see @ref net "Networking"
   @see @ref LNetDLD "LNet Transport DLD"
   @see @ref LNetDFS "LNet Transport Interface"

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
	/** Default number of threads in a transfer machine */
	C2_NET_LNET_TM_DEF_NUM_THREADS = 1,
	/** @todo Max LNet ep addr length?, 4-tuple */
	C2_NET_LNET_XEP_ADDR_LEN = 80,
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
   Sets the number of threads to use in an LNet transfer machine.
   The default is ::C2_NET_LNET_TM_DEF_NUM_THREADS.
 */
extern int c2_net_lnet_tm_set_num_threads(struct c2_net_transfer_mc *tm,
					  uint32_t num_threads);

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

