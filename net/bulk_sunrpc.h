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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */

#ifndef __COLIBRI_NET_BULK_SUNRPC_H__
#define __COLIBRI_NET_BULK_SUNRPC_H__

#include "net/net.h"

/**
   @defgroup bulksunrpc Sunrpc Messaging and Bulk Transfer Emulation Transport
   @ingroup net

   This module provides a network transport with messaging and bulk
   transfer capabilites implemented over the legacy and now deprecated
   Sunrpc transport.  The bulk transfer support does not provide 0 copy
   semantics. 3-tuple addressing of (host, port, service-id) is used for
   end points, with the further constraint that all transfer machines within
   the same process must use the same host and port, regardless of how many
   bulksunrpc domains are created.

   The transport will fake support for synchronous network buffer delivery.

   @{
**/

/**
   The bulk sunrpc transport pointer to be used in c2_net_domain_init().
 */
extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;

/**
   Set the number of worker threads used by a bulk sunrpc transfer machine.
   This can be changed before the the transfer machine has started.
   @param tm  Pointer to the transfer machine.
   @param num Number of threads.
   @pre tm->ntm_state == C2_NET_TM_INITALIZED
 */
void c2_net_bulk_sunrpc_tm_set_num_threads(struct c2_net_transfer_mc *tm,
					  size_t num);

/**
   Return the number of threads used by a bulk sunrpc transfer machine.
   @param tm  Pointer to the transfer machine.
   @retval Number-of-threads
 */
size_t c2_net_bulk_sunrpc_tm_get_num_threads(const struct c2_net_transfer_mc
					     *tm);

/**
   Obtain the period of the skulker thread clock.
   @retval secs Period in seconds.
 */
uint64_t
c2_net_bulk_sunrpc_dom_get_skulker_period(struct c2_net_domain *dom);

/**
   Control the period of the skulker thread clock.  This thread is used
   to handle end point aging and buffer timeouts.
   @param dom The domain pointer.
   @param secs The clock period in seconds. Must be greater than 0.
 */
void
c2_net_bulk_sunrpc_dom_set_skulker_period(struct c2_net_domain *dom,
					  uint64_t secs);

/**
   Control how long unused end points are cached before release.

   The delay allows potential reuse of the underlying network connection at a
   later time.  This avoids exhaustion of the local dynamic port space,
   as a port normally goes into TIMED_WAIT when the socket closes and won't
   be made available for reuse until much later.  It also has the added
   benefit of reducing the number of TCP connections established.

   @param dom The domain pointer.
   @param secs The duration of the delay in seconds.  Specify 0 for no
   delay.  The default is to delay.
 */
void
c2_net_bulk_sunrpc_dom_set_end_point_release_delay(struct c2_net_domain *dom,
						   uint64_t secs);

/**
   Return the end point release delay value.
   @param dom The domain pointer.
   @retval secs Returns the seconds of delay, or 0 if delay is disabled.
*/
uint64_t
c2_net_bulk_sunrpc_dom_get_end_point_release_delay(struct c2_net_domain *dom);

#ifdef __KERNEL__
#define C2_NET_SUNRPC_PORT  31110
#else
#define C2_NET_SUNRPC_PORT  31111
#endif
#define str(x) #x
#define SUNRPC_XPRT_ADDR(port, service) "127.0.0.1:"str(port)":"str(service)
#define EP_SERVICE(sid) SUNRPC_XPRT_ADDR(C2_NET_SUNRPC_PORT, sid)

/**
   @}
*/

#endif /* __COLIBRI_NET_BULK_SUNRPC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
