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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 11/10/2011
 */

/**
   @defgroup bevqueue LNet Buffer Event Queue Interface
   @ingroup LNetDFS

   The buffer event FIFO queue, used between the LNet Core and LNet transport.

   Unlike the standard c2_queue, this queue supports a producer and consumer
   in different address spaces sharing the queue via shared memory.

   @{
 */

/**
   Buffer event queue invariant.
 */
static bool bev_queue_invariant(const struct c2_queue *q);

/**
   Initialise the buffer event queue.
 */
static void bev_queue_init(struct c2_lnet_core_bev_queue *q);
/**
   Finalise the buffer event queue.
 */
static void bev_queue_fini(struct c2_lnet_core_bev_queue *q);

/**
   Test if the buffer event queue is empty.
 */
static bool bev_queue_is_empty(const struct c2_lnet_core_bev_queue *q);

/**
   Get the oldest element in the FIFO queue.
   @param q the queue
   @returns the link to the element, NULL when the queue is empty
 */
static struct c2_lnet_core_bev_link *bev_queue_get(
					     struct c2_lnet_core_bev_queue *q);
/**
   Put a new entry into the FIFO queue.
   @param q the queue
   @param ql link of the element to add to the end of the queue
   @pre ql->lcbevl_c_next == NULL && q->lcbevq_producer != ql
 */
static void bev_queue_put(struct c2_lnet_core_bev_queue *q,
			  struct c2_lnet_core_bev_link *ql);

/**
   @}
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
