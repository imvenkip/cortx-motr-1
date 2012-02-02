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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
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

   Some LNet transport idiocyncrasies to be aware of:
   - LNet does not provide any guarantees to a sender of data that the data was
     actually received by its intended recipient.  In LNet semantics, a
     successful buffer completion callback for C2_NET_QT_MSG_SEND and
     C2_NET_QT_ACTIVE_BULK_SEND only indicates that the data was successfully
     transmitted from the buffer and that the buffer can be reused.  It does
     not provide any indication if recipient was able to store the data or not.
     This makes it very important for an application to keep the unsolicited
     receive message queue (C2_NET_QT_MSG_RECV) populated at all times.

   - Similar to the previous case, LNet may not provide indication that an end
     point address is invalid during buffer operations. A C2_NET_QT_MSG_SEND
     operation may succeed even if the end point provided as the destination
     address does not exist.

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
	/** Maximum LNet end point address length.
	    @todo XXX Determine exact value for a 4-tuple LNet EP addr
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
   Gets a list of strings corresponding to the local LNET network interfaces.
   The returned array must be released using c2_net_lnet_ifaces_put().
   @param addrs A NULL-terminated (like argv) array of NID strings is returned.
 */
int c2_net_lnet_ifaces_get(char * const **addrs);

/**
   Releases the string array returned by c2_net_lnet_ifaces_get().
 */
void c2_net_lnet_ifaces_put(char * const **addrs);

/* init and fini functions for colibri init */
int c2_net_lnet_init(void);
void c2_net_lnet_fini(void);

/*
   Debug support.
 */
void c2_net_lnet_dom_set_debug(struct c2_net_domain *dom, unsigned dbg);
void c2_net_lnet_tm_set_debug(struct c2_net_transfer_mc *tm, unsigned dbg);

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
