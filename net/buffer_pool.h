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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 10/12/2011
 */

#ifndef __COLIBRI_BUFFER_POOL_H__
#define __COLIBRI_BUFFER_POOL_H__

#include "lib/types.h" /* uint64_t */
#include "lib/mutex.h"
#include "net/net.h"   /* c2_net_buffer, c2_net_domain */
#include "lib/tlist.h"

/**
   @defgroup net_buffer_pool Network Buffer Pool

   @brief Network buffer pool allocates and manages a pool of network buffers.
	  Users request a buffer from the pool and after its usage is over
	  gives back to the pool.

	  It provides suppport for a pool of network buffers involving no higher	  level interfaces than the network module itself.
	  It is associated with a single network domain.
	  Non-blocking interfaces are available to get and put network buffers
	  and callbacks to signal the availability of buffers and low
	  threshold is reached are provided.

	  Upon receiving the not_empty call back user can put back buffers which	  are not in use into the pool.

	  The “coloured” variant of the get operation is done here by returning 	  the most recently used buffer last associated with a specific transfer
	  machine, or if none such are found, a buffer which has no previous
	  transfer machine association, or if none such are found, the least
	  recently used buffer from the pool, if any.

	  Pool is protected by a lock, to get or put a buffer into the pool user	  must acquire the lock and release the lock once its usage is over.

	  To finalize the pool all the buffers must be returned back to the pool	  (i.e number of free buffers must be equal to the total number of of
	  buffers).

 To describe a typical buffer pool usage pattern, suppose that one wants
 a buffer pool of 10, size of each segment is 1024, number of segments is
 64 and threshold is 10.

    First, user needs to provide c2_net_buffer_pool_ops:
    @code
	struct c2_net_buffer_pool_ops b_ops = {
		.nbpo_not_empty	      = NotEmpty,
		.nbpo_below_threshold = Low,
	};
    @endcode

   - Then, buffer pool needs to be assigned to a network domain and initialized
	with above values:
    @code
	struct c2_net_buffer_pool bp;
	struct c2_net_xprt *xprt;
	xprt = &c2_net_bulk_sunrpc_xprt;
	c2_net_xprt_init(xprt);
	C2_ALLOC_PTR(bp.nbp_ndom);
	rc = c2_net_domain_init(bp.nbp_ndom, xprt);
	bp.nbp_ops = &b_ops;
	c2_net_buffer_pool_init(&bp, bp.nbp_ndom, 10, 64, 4096);
	...
    @endcode

   - Now, to add buffers into the pool need to acquire the lock and then specify
	the number of buffers to be added:
    @code
	c2_net_buffer_pool_lock(&bp);
	c2_net_buffer_pool_provision(&bp, 10);
	c2_net_buffer_pool_unlock(&bp);
    @endcode

    - To add a buffer in the pool:
    @code
	c2_net_buffer_pool_lock(&bp);
	c2_net_buffer_pool_provision(&bp, 1);
	c2_net_buffer_pool_unlock(&bp);
    @endcode

    - To get a buffer from the pool:
    @code
	c2_net_buffer_pool_lock(&bp);
	nb = c2_net_buffer_pool_get(&bp);
	c2_net_buffer_pool_unlock(&bp);
    @endcode

   - To put back the buffer in the pool:
    @code
	c2_net_buffer_pool_lock(&bp);
	c2_net_buffer_pool_put(&bp, nb);
	c2_net_buffer_pool_unlock(&bp);
    @endcode

    - To remove a buffer from the pool:
    @code
	c2_net_buffer_pool_lock(&bp);
	c2_net_buffer_pool_prune(&bp);
	c2_net_buffer_pool_unlock(&bp);
    @endcode

    - To finalize the pool:
    @code
	c2_net_buffer_pool_lock(&bp);
	c2_net_buffer_pool_fini(&bp);
	c2_net_buffer_pool_unlock(&bp);
    @endcode
   @{
  */

struct c2_net_buffer_pool;

/** Call backs that buffer pool can trigger on different memory conditions. */
struct c2_net_buffer_pool_ops {
	/** Buffer pool is not empty. */
	void (*nbpo_not_empty)(struct c2_net_buffer_pool *);
	/** Buffers in pool are lower than threshold. */
	void (*nbpo_below_threshold)(struct c2_net_buffer_pool *);
};

/** Checks the buffer pool. */
bool c2_net_buffer_pool_invariant(const struct c2_net_buffer_pool *pool);

/**
   Initializes fields of a buffer pool and tlist, which are used to populate
   the pool using c2_net_buffer_pool_provision().
   @pre ndom != NULL
   @param threshold Number of buffer below which to notify the user.
   @param seg_nr    Number of segments in each buffer.
   @param seg_size  Size of each segment in a buffer.
   @pre (seg_nr * seg_size) <= c2_net_domain_get_max_buffer_size(ndom) &&
	seg_size <= c2_net_domain_get_max_buffer_segment_size(ndom)
   @post c2_net_buffer_pool_invariant(pool)
 */
void c2_net_buffer_pool_init(struct c2_net_buffer_pool *pool,
			    struct c2_net_domain *ndom, uint32_t threshold,
			    uint32_t seg_nr, c2_bcount_t seg_size);

/**
   It adds the buf_nr buffers in the buffer pool.
   Suppose to add 10 items to the pool, c2_net_buffer_pool_provison(pool, 10)
   can be used.
   @pre c2_net_buffer_pool_is_locked(pool)
   @pre seg_size > 0 && seg_nr > 0 && buf_nr > 0
   @pre pool->nbp_ndom != NULL
   @param buf_nr Number of buffers to be added in the pool.
   @return result number of buffers it managed to allocate.
*/
int c2_net_buffer_pool_provision(struct c2_net_buffer_pool *pool,
				 uint32_t buf_nr);
/** Finalizes a buffer pool.
   @pre c2_net_buffer_pool_is_locked(pool)
 */
void c2_net_buffer_pool_fini(struct c2_net_buffer_pool *pool);

/** Acquires the lock on buffer pool. */
void c2_net_buffer_pool_lock(struct c2_net_buffer_pool *pool);

/** Check whether buffer pool is locked or not. */
bool c2_net_buffer_pool_is_locked(const struct c2_net_buffer_pool *pool);

/** Releases the lock on buffer pool. */
void c2_net_buffer_pool_unlock(struct c2_net_buffer_pool *pool);

/**
   Gets a buffer from the pool.
   If transfer machine is not null then a linear search from the head of the
   list will break off when a buffer of the correct affinity is found, or a
   buffer with no affinity is found, or else the buffer at the tail of the list    is selected.
   If transfer machine is not specified buffer will be taken from the tail of
   the list.
   Returns NULL when the pool is empty.
   @pre c2_net_buffer_pool_is_locked(pool)
   @post ergo(result != NULL, result->nb_flags & C2_NET_BUF_REGISTERED)
 */
struct c2_net_buffer *c2_net_buffer_pool_get(struct c2_net_buffer_pool *pool,
					     struct c2_net_transfer_mc *tm);

/**
   Puts the buffer back to the pool.
   Buffers which are associated with a transfer machine are put at the head of
   the list and buffers with no association are put at the tail of the list.
   @pre c2_net_buffer_pool_is_locked(pool)
   @pre pool->nbp_ndom == buf->nb_dom
   @pre (buf->nb_flags & C2_NET_BUF_REGISTERED) &&
        !(buf->nb_flags & C2_NET_BUF_IN_USE)
 */
void c2_net_buffer_pool_put(struct c2_net_buffer_pool *pool,
			    struct c2_net_buffer *buf);

/**
   Removes a buffer from the pool to prune it.
   @pre c2_net_buffer_pool_is_locked(pool)
 */
bool c2_net_buffer_pool_prune(struct c2_net_buffer_pool *pool);

/** Buffer pool. */
struct c2_net_buffer_pool {
	/** Number of free buffers in the pool. */
	uint32_t		nbp_free;
	/** Number of buffer below which low memory condition occurs. */
	uint32_t		nbp_threshold;
	/** Number of segments in each buffer of the pool. */
	uint32_t		nbp_seg_nr;
	/** Number of buffers in the pool. */
	uint32_t		nbp_buf_nr;
	/** Size of buffer segment of the pool. */
	c2_bcount_t		nbp_seg_size;
	/** Buffer pool lock to protect and synchronize network buffer list.
	    It needs to acquired to do any changes to the pool
	 */
	struct c2_mutex		nbp_mutex;
	/** Network domain to register the buffers. */
	struct c2_net_domain   *nbp_ndom;
	/** Head of list of buffers in the pool. */
	struct c2_tl		nbp_head;
	/** Call back operations can be triggered by buffer pool. */
	const struct c2_net_buffer_pool_ops *nbp_ops;
};

/** @} end of net_buffer_pool */
#endif
