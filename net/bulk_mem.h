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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */
#ifndef __COLIBRI_NET_BULK_MEM_H__
#define __COLIBRI_NET_BULK_MEM_H__

#include "net/net.h"

/**
   @defgroup bulkmem In-Memory Messaging and Bulk Transfer Emulation Transport

   @brief This module provides a network transport with messaging and bulk
   data transfer across domains within a single process.

   An end point address is a string with two tuples separated by a colon (:).
   The first tuple is the dotted decimal representation of an IPv4 address,
   and the second is an IP port number.

   When used as a base for a derived transport, a third tuple representing
   an unsigned 32 bit service identifier is supported in the address.

   @{
**/

enum {
	C2_NET_BULK_MEM_XEP_ADDR_LEN = 36 /**< Max addr length, 3-tuple */
};

/**
   The bulk in-memory transport pointer to be used in c2_net_domain_init().
 */
extern struct c2_net_xprt c2_net_bulk_mem_xprt;

/**
   Set the number of worker threads used by a bulk in-memory transfer machine.
   This can be changed before the the transfer machine has started.
   @param tm  Pointer to the transfer machine.
   @param num Number of threads.
   @pre tm->ntm_state == C2_NET_TM_INITIALZIED
 */
void c2_net_bulk_mem_tm_set_num_threads(struct c2_net_transfer_mc *tm,
					size_t num);

/**
   Return the number of threads used by a bulk in-memory transfer machine.
   @param tm  Pointer to the transfer machine.
   @retval Number-of-threads
 */
size_t c2_net_bulk_mem_tm_get_num_threads(const struct c2_net_transfer_mc *tm);


/**
   @}
*/

#endif /* __COLIBRI_NET_BULK_MEM_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
