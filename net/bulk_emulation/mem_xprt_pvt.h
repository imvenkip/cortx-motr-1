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

/**
   Content on the network descriptor.
 */
struct mem_desc {
	/** Address of the active end point */
	struct sockaddr_in     md_active;
	/** Address of the passive end point */
	struct sockaddr_in     md_passive;
	/** Queue type */
	enum c2_net_queue_type md_qt;
	/** Data length */
	c2_bcount_t            md_len;
};

/* forward references to other static functions */
static bool mem_dom_invariant(struct c2_net_domain *dom);
static bool mem_ep_invariant(struct c2_net_end_point *ep);
static bool mem_buffer_invariant(struct c2_net_buffer *nb);
static bool mem_tm_invariant(struct c2_net_transfer_mc *tm);
static int mem_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_domain *dom,
			 struct sockaddr_in *sa);
static bool mem_eps_are_equal(struct c2_net_end_point *ep1,
			      struct c2_net_end_point *ep2);
static bool mem_ep_equals_addr(struct c2_net_end_point *ep,
			       struct sockaddr_in *sa);
static int mem_desc_create(struct c2_net_buf_desc *desc,
			   struct c2_net_end_point *ep,
			   struct c2_net_transfer_mc *tm,
			   enum c2_net_queue_type qt,
			   c2_bcount_t buflen);
static int mem_desc_decode(struct c2_net_buf_desc *desc,
			   struct mem_desc **p_md);
static bool mem_desc_equal(struct c2_net_buf_desc *d1,
			   struct c2_net_buf_desc *d2);
static c2_bcount_t mem_buffer_length(struct c2_net_buffer *nb);
static bool mem_buffer_in_bounds(struct c2_net_buffer *nb);
static int mem_copy_buffer(struct c2_net_buffer *dest_nb,
			   struct c2_net_buffer *src_nb,
			   c2_bcount_t num_bytes);
static void mem_wi_add(struct c2_net_bulk_mem_work_item *wi,
		       struct c2_net_bulk_mem_tm_pvt *tp);

#ifdef MEM_WI_TO_BUFFER
#undef MEM_WI_TO_BUFFER
#endif
/**
   Macro to obtain the c2_net_buffer pointer from its related work item.
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

#ifdef MEM_BUFFER_TO_WI
#undef MEM_BUFFER_TO_WI
#endif
/**
   Macro to obtain the work item from a c2_net_buffer pointer.
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

#ifdef MEM_CUR_ADDR
#undef MEM_CUR_ADDR
#endif
/**
   Macro to get the buffer address from a vector cursor.
   @param nb Buffer pointer (struct c2_net_buffer *)
   @param cur Cursor pointer (struct c2_vec_cursor *)
 */
#define MEM_CUR_ADDR(nb,cur)    \
 (&(nb)->nb_buffer.ov_buf[(cur)->vc_seg] + (cur)->vc_offset)

#ifdef MEM_SA_EQ
#undef MEM_SA_EQ
#endif
/**
   Macro to compare two struct sockaddr_in structures.
   @param sa1 Pointer to first structure.
   @param sa2 Pointer to second structure.
 */
#define MEM_SA_EQ(sa1,sa2)				\
 (sa1)->sin_addr.s_addr == (sa2)->sin_addr.s_addr &&	\
 (sa1)->sin_port        == (sa2)->sin_port

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
