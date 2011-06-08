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
 * Original author: Carl Braganza, Dave Cohrs
 * Original creation date: 04/12/2011
 */
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
static bool mem_dom_invariant(const struct c2_net_domain *dom);
static bool mem_ep_invariant(const struct c2_net_end_point *ep);
static bool mem_buffer_invariant(const struct c2_net_buffer *nb);
static bool mem_tm_invariant(const struct c2_net_transfer_mc *tm);
static int mem_ep_create(struct c2_net_end_point **epp,
			 struct c2_net_domain *dom,
			 const struct sockaddr_in *sa,
			 uint32_t id);
static bool mem_eps_are_equal(const struct c2_net_end_point *ep1,
			      const struct c2_net_end_point *ep2);
static bool mem_ep_equals_addr(const struct c2_net_end_point *ep,
			       const struct sockaddr_in *sa);
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
static c2_bcount_t mem_buffer_length(const struct c2_net_buffer *nb);
static bool mem_buffer_in_bounds(const struct c2_net_buffer *nb);
static int mem_copy_buffer(struct c2_net_buffer *dest_nb,
			   struct c2_net_buffer *src_nb,
			   c2_bcount_t num_bytes);
static void mem_wi_add(struct c2_net_bulk_mem_work_item *wi,
		       struct c2_net_bulk_mem_tm_pvt *tp);
static void mem_post_error(struct c2_net_transfer_mc *tm, int status);
static void mem_wi_post_buffer_event(struct c2_net_bulk_mem_work_item *wi);

/**
   Macro to compare two struct sockaddr_in structures.
   @param sa1 Pointer to first structure.
   @param sa2 Pointer to second structure.
 */
static inline bool mem_sa_eq(const struct sockaddr_in *sa1,
			     const struct sockaddr_in *sa2)
{
	return sa1->sin_addr.s_addr == sa2->sin_addr.s_addr &&
	       sa1->sin_port        == sa2->sin_port;
}

/**
   Function to indirectly invoke the mem_ep_create subroutine via the domain
   function pointer, to support derived transports.
   @see mem_ep_create()
 */
static inline int mem_bmo_ep_create(struct c2_net_end_point **epp,
				    struct c2_net_domain *dom,
				    const struct sockaddr_in *sa,
				    uint32_t id)
{
	struct c2_net_bulk_mem_domain_pvt *dp = dom->nd_xprt_private;
	return dp->xd_ops->bmo_ep_create(epp, dom, sa, id);
}

/**
   Function to indirectly invoke the mem_buffer_in_bounds subroutine via the
   domain function pointer, to support derived transports.
   @see mem_buffer_in_bounds()
 */
static inline bool mem_bmo_buffer_in_bounds(const struct c2_net_buffer *nb)
{
	struct c2_net_bulk_mem_domain_pvt *dp = nb->nb_dom->nd_xprt_private;
	return dp->xd_ops->bmo_buffer_in_bounds(nb);
}

/**
   Function to indirectly invoke the mem_desc_create subroutine via the domain
   function pointer, to support derived transports.
   @see mem_desc_create()
 */
static int mem_bmo_desc_create(struct c2_net_buf_desc *desc,
			       struct c2_net_end_point *ep,
			       struct c2_net_transfer_mc *tm,
			       enum c2_net_queue_type qt,
			       c2_bcount_t buflen,
			       int64_t buf_id)
{
	struct c2_net_bulk_mem_domain_pvt *dp = tm->ntm_dom->nd_xprt_private;
	return dp->xd_ops->bmo_desc_create(desc, ep, tm, qt, buflen, buf_id);
}

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
