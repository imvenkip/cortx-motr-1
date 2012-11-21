/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

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
   - c2_net_lnet_ifaces_get()
     Get a reference to the list of registered LNet interfaces, as strings.
   - c2_net_lnet_ifaces_put()
     Releases the reference to the list of registered LNet interfaces.
   - c2_net_lnet_tm_stat_interval_set()
     Sets the interval at which a started LNet transfer machine will generate
     ADDB events with transfer machine statistics.
   - c2_net_lnet_tm_stat_interval_get()
     Gets the current statistics interval.

   The use of these subroutines is not mandatory.

   @see @ref net "Networking"                     <!-- net/net.h -->
   @see @ref LNetDLD "LNet Transport DLD"         <!-- net/lnet/lnet_main.c -->
   @see @ref LNetDFS "LNet Transport"   <!-- below -->

 */

#include "net/net.h"

/**
   @defgroup LNetDFS LNet Transport
   @ingroup net

   The external interfaces of the LNet transport are obtained by including the
   file @ref net/lnet/lnet.h.  The ::c2_net_lnet_xprt variable represents the
   transport itself and is used as an argument to c2_net_domain_init().

   An end point address for this transport is of the form:
   @code
     NetworkIdentifierString : PID : PortalNumber : TransferMachineIdentifier
   @endcode

   For example:
   @code
     10.72.49.14@o2ib0:12345:31:0
     192.168.96.128@tcp1:12345:32:*
   @endcode
   The PID value of 12345 is used by Lustre
   in the kernel and is the only value currently supported. The symbolic
   constant ::C2_NET_LNET_PID provides this value.

   The "*" indicates a dynamic assignment of a transfer machine identifier.
   This syntax is valid only when starting a transfer machine with the
   c2_net_tm_start() subroutine; it is intended for use by ephemeral processes
   like management utilities and user interactive programs, not by servers.

   Some LNet transport idiosyncrasies to be aware of:
   - LNet does not provide any guarantees to a sender of data that the data was
     actually received by its intended recipient.  In LNet semantics, a
     successful buffer completion callback for ::C2_NET_QT_MSG_SEND and
     ::C2_NET_QT_ACTIVE_BULK_SEND only indicates that the data was successfully
     transmitted from the buffer and that the buffer can be reused.  It does
     not provide any indication if recipient was able to store the data or not.
     This makes it very important for an application to keep the unsolicited
     receive message queue (::C2_NET_QT_MSG_RECV) populated at all times.

   - Similar to the previous case, LNet may not provide indication that an end
     point address is invalid during buffer operations. A ::C2_NET_QT_MSG_SEND
     operation may succeed even if the end point provided as the destination
     address does not exist.

   @see @ref LNetDLD "LNet Transport DLD"

   @{
 */

/**
   The LNet transport is used by specifying this data structure to the
   c2_net_domain_init() subroutine.
 */
extern struct c2_net_xprt c2_net_lnet_xprt;

enum {
	/**
	   The Lustre PID value used in the kernel.
	 */
	C2_NET_LNET_PID = 12345,
	/**
	   Maximum LNet end point address length.
	*/
	C2_NET_LNET_XEP_ADDR_LEN = 80,
	/**
	   Report TM statistics once every 5 minutes by default.
	 */
	C2_NET_LNET_TM_STAT_INTERVAL_SECS = 60 * 5,
};

/**
   Subroutine compares the network portions of two LNet end point address
   strings.
   @retval -1 if any of the two strings do not have a colon character.
   @retval int Return value like strcmp().
 */
C2_INTERNAL int c2_net_lnet_ep_addr_net_cmp(const char *addr1,
					    const char *addr2);

/**
   Gets a list of strings corresponding to the local LNET network interfaces.
   The returned array must be released using c2_net_lnet_ifaces_put().
   @param dom Pointer to the domain.
   @param addrs A NULL-terminated (like argv) array of NID strings is returned.
 */
C2_INTERNAL int c2_net_lnet_ifaces_get(struct c2_net_domain *dom,
				       char *const **addrs);

/**
   Releases the string array returned by c2_net_lnet_ifaces_get().
 */
C2_INTERNAL void c2_net_lnet_ifaces_put(struct c2_net_domain *dom,
					char *const **addrs);

/**
   Sets the transfer machine statistics reporting interval.
   By default, the interval is @c C2_NET_LNET_TM_STAT_INTERVAL_SECS seconds.
   @param tm   Pointer to the transfer machine.
   @param secs The interval in seconds. Must be greater than 0.
   @pre tm->ntm_state >= C2_NET_TM_INITIALIZED &&
   tm->ntm_state <= C2_NET_TM_STOPPING &&
   secs > 0
 */
C2_INTERNAL void c2_net_lnet_tm_stat_interval_set(struct c2_net_transfer_mc *tm,
						  uint64_t secs);

/**
   Gets the transfer machine statistics reporting interval.
   @param tm  Pointer to the transfer machine.
   @pre tm->ntm_state >= C2_NET_TM_INITIALIZED &&
   tm->ntm_state <= C2_NET_TM_STOPPING
 */
C2_INTERNAL uint64_t c2_net_lnet_tm_stat_interval_get(struct c2_net_transfer_mc
						      *tm);

/* init and fini functions for colibri init */
C2_INTERNAL int c2_net_lnet_init(void);
C2_INTERNAL void c2_net_lnet_fini(void);

/*
   Debug support.
 */
C2_INTERNAL void c2_net_lnet_dom_set_debug(struct c2_net_domain *dom,
					   unsigned dbg);
C2_INTERNAL void c2_net_lnet_tm_set_debug(struct c2_net_transfer_mc *tm,
					  unsigned dbg);

/**
   @}
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
