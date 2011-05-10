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
	/** buffer id */
	int64_t                md_buf_id;
};

/* forward references to other static functions */
static bool mem_dom_invariant(struct c2_net_domain *dom);
static bool mem_ep_invariant(struct c2_net_end_point *ep);
static bool mem_buffer_invariant(struct c2_net_buffer *nb);
static bool mem_tm_invariant(struct c2_net_transfer_mc *tm);
static int mem_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_domain *dom,
			 struct sockaddr_in *sa,
			 uint32_t id);
static bool mem_eps_are_equal(struct c2_net_end_point *ep1,
			      struct c2_net_end_point *ep2);
static bool mem_ep_equals_addr(struct c2_net_end_point *ep,
			       struct sockaddr_in *sa);
static int mem_desc_create(struct c2_net_buf_desc *desc,
			   struct c2_net_end_point *ep,
			   struct c2_net_transfer_mc *tm,
			   enum c2_net_queue_type qt,
			   c2_bcount_t buflen,
			   int64_t buf_id);
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
static void mem_post_error(struct c2_net_transfer_mc *tm, int status);

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

#ifdef MEM_EP_CREATE
#undef MEM_EP_CREATE
#endif
/**
   Macro to indirectly invoke the mem_ep_create subroutine via the domain
   function pointer, to support derived transports.
 */
#define MEM_EP_CREATE(epp, dom, sa, id)					\
({									\
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;	\
	dp->xd_ops.bmo_ep_create(epp, dom, sa, id);			\
 })

#ifdef MEM_BUFFER_IN_BOUNDS
#undef MEM_BUFFER_IN_BOUNDS
#endif
/**
   Macro to indirectly invoke the mem_buffer_in_bounds subroutine via the
   domain function pointer, to support derived transports.
 */
#define MEM_BUFFER_IN_BOUNDS(nb)					\
({									\
	struct c2_net_bulk_mem_domain_pvt *dp = nb->nb_dom->nd_xprt_private;\
	dp->xd_ops.bmo_buffer_in_bounds(nb);				\
 })

#ifdef MEM_DESC_CREATE
#undef MEM_DESC_CREATE
#endif
/**
   Macro to indirectly invoke the mem_desc_create subroutine via the domain
   function pointer, to support derived transports.
 */
#define MEM_DESC_CREATE(desc, ep, tm, qt, buflen, buf_id)		\
({									\
	struct c2_net_bulk_mem_domain_pvt *dp = tm->ntm_dom->nd_xprt_private;\
	dp->xd_ops.bmo_desc_create(desc, ep, tm, qt, buflen, buf_id);	\
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
