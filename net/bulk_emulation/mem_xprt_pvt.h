/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_MEM_XPRT_PVT_H__
#define __COLIBRI_NET_BULK_MEM_XPRT_PVT_H__

#include "lib/errno.h"
#include "net/bulk_emulation/mem_xprt.h"

/**
   @addtogroup bulkmem

   @{
*/

enum {
	C2_NET_BULK_MEM_MAX_BUFFER_SIZE = (1<<20),
	C2_NET_BULK_MEM_MAX_SEGMENT_SIZE = (1<<20),
	C2_NET_BULK_MEM_MAX_BUFFER_SEGMENTS = 256
};

/**
   List of in-memory network domains.
   Protected by struct c2_net_mutex.
*/
extern struct c2_list  c2_net_bulk_mem_domains;

bool c2_net_bulk_mem_dom_invariant(struct c2_net_domain *dom);
bool c2_net_bulk_mem_ep_invariant(struct c2_net_end_point *ep);
bool c2_net_bulk_mem_buffer_invariant(struct c2_net_buffer *nb);
bool c2_net_bulk_mem_tm_invariant(struct c2_net_transfer_mc *tm);

/**
   Create a network buffer descriptor from an in-memory end point.
 */
int c2_net_bulk_mem_ep_create_desc(struct c2_net_end_point *ep,
				   struct c2_net_buf_desc *desc);

/**
   @}
*/


#endif /* __COLIBRI_NET_BULK_MEM_XPRT_PVT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
