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

/* forward references to other static functions */
static bool mem_dom_invariant(struct c2_net_domain *dom);
static bool mem_ep_invariant(struct c2_net_end_point *ep);
static bool mem_buffer_invariant(struct c2_net_buffer *nb);
static bool mem_tm_invariant(struct c2_net_transfer_mc *tm);
static int mem_ep_create_desc(struct c2_net_end_point *ep,
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
