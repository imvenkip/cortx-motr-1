/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_MEM_H__
#define __COLIBRI_NET_BULK_MEM_H__

#include "net/net.h"

/**
   @defgroup bulkmem In-Memory Messaging and Bulk Transfer Emulation Transport

   @brief This module provides a network transport with messaging and bulk
   data transfer across domains within a single process.

   @{
**/

/**
   The bulk in-memory transport pointer to be used in c2_net_domain_init().
 */
extern struct c2_net_xprt c2_net_bulk_mem_xprt;

/**
   Set the number of worker threads used by a bulk in-memory transfer machine.
   This can be changed before the the transfer machine has started.
   @param tm  Pointer to the transfer machine.
   @param num Number of threads.
   @retval 0 on failure
   @retval -EPERM Transfer machine has already been started.
 */
int c2_net_bulk_mem_tm_set_num_threads(struct c2_net_transfer_mc *tm,
				       size_t num);

/**
   Return the number of threads used by a bulk in-memory transfer machine.
   @param tm  Pointer to the transfer machine.
   @retval Number-of-threads
 */
size_t c2_net_bulk_mem_tm_get_num_threads(struct c2_net_transfer_mc *tm);


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
