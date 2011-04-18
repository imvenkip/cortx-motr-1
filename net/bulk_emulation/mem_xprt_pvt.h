/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_MEM_XPRT_PVT_H__
#define __COLIBRI_NET_BULK_MEM_XPRT_PVT_H__

#include "lib/errno.h"
#include "net/net_internal.h"
#include "net/bulk_emulation/mem_xprt.h"

/**
   @addtogroup bulkmem

   @{
*/

enum {
	C2_NET_BULK_MEM_MAX_BUFFER_SIZE = (1<<20),
	C2_NET_BULK_MEM_MAX_SEGMENT_SIZE = (1<<20),
	C2_NET_BULK_MEM_MAX_BUFFER_SEGMENTS = 256,
};

/* forward references to other static functions */
static bool mem_dom_invariant(struct c2_net_domain *dom);
static bool mem_ep_invariant(struct c2_net_end_point *ep);
static bool mem_buffer_invariant(struct c2_net_buffer *nb);
static bool mem_tm_invariant(struct c2_net_transfer_mc *tm);
static int mem_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_domain *dom,
			 struct sockaddr_in *sa);
static bool mem_ep_is_equal(struct c2_net_end_point *ep1,
			    struct c2_net_end_point *ep2);
static int mem_ep_create_desc(struct c2_net_end_point *ep,
			      struct c2_net_buf_desc *desc);
static c2_bcount_t mem_buffer_length(struct c2_net_buffer *nb);
static bool mem_buffer_in_bounds(struct c2_net_buffer *nb);
static int mem_copy_buffer(struct c2_net_buffer *dest_nb,
			   struct c2_net_buffer *src_nb,
			   c2_bcount_t num_bytes);
static void mem_wi_add(struct c2_net_bulk_mem_work_item *wi,
		       struct c2_net_bulk_mem_tm_pvt *tp);

/**
   Obtain the c2_net_buffer pointer from its related work item.
@code
struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
@endcode
 */
#define MEM_WI_TO_BUFFER(wi)						\
({									\
	struct c2_net_bulk_mem_buffer_pvt *bp;				\
	bp = container_of(wi, struct c2_net_bulk_mem_buffer_pvt, xb_wi);\
	bp->xb_buffer;							\
})

/**
   Obtain the work item from a c2_net_buffer pointer.
@code
struct c2_net_bulk_mem_work_item *wi = MEM_BUFFER_TO_WI(nb)
@endcode
 */
#define MEM_BUFFER_TO_WI(buf)			\
({						\
	struct c2_net_bulk_mem_buffer_pvt *bp;	\
	bp = buf->nb_xprt_private;		\
	&bp->xb_wi;				\
})

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
