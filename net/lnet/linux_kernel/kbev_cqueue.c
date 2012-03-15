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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 3/6/2012
 */

/**
   @addtogroup bevcqueue
   @{
 */

/**
   Atomically map the buffer event link corresponding to the
   given memory reference.
 */
static inline struct nlx_core_bev_link* bev_link_map(
						 struct nlx_core_kmem_loc *loc)
{
	/** @todo implement */
	return NULL;
}

/**
   Unmap the buffer event link corresponding to the given memory reference.
 */
static inline void bev_link_unmap(struct nlx_core_kmem_loc *loc)
{
	/** @todo implement */
}

/**
   Determines the next element in the queue that can be used by the producer.
   @note This operation is to be used only by the producer.
   @param q the queue
   @returns a pointer to the next available element in the producer context
   @pre p->cbl_c_self != q->cbcq_consumer
 */
static struct nlx_core_bev_link* bev_cqueue_pnext(
				      const struct nlx_core_bev_cqueue *q)
{
	struct nlx_core_bev_link* p;

	C2_PRE(bev_cqueue_invariant(q));
	p = (struct nlx_core_bev_link*) q->cbcq_producer;
	C2_PRE(p->cbl_c_self != q->cbcq_consumer);
	return p;
}

/**
   Puts (produces) an element so it can be consumed.  The caller must first
   call bev_cqueue_pnext() to ensure such an element exists.
   @param q the queue
   @pre p->cbl_c_self != q->cbcq_consumer
 */
static void bev_cqueue_put(struct nlx_core_bev_cqueue *q)
{
	struct nlx_core_bev_link* p;

	C2_PRE(bev_cqueue_invariant(q));
	p = (struct nlx_core_bev_link*) q->cbcq_producer;
	C2_PRE(p->cbl_c_self != q->cbcq_consumer);
	q->cbcq_producer = p->cbl_p_next;
	c2_atomic64_inc(&q->cbcq_count);
}

/**
   Blesses the nlx_core_bev_link of a nlx_core_bev_cqueue element, assigning
   the producer self value.
   @param ql the link to bless, the caller must have already mapped the element
   into the producer address space.
 */
static void bev_link_bless(struct nlx_core_bev_link *ql)
{
	ql->cbl_p_self = (nlx_core_opaque_ptr_t) ql;
}

/** @} */ /* bevcqueue */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
