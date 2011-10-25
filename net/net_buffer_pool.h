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
 * Original creation date: 12/10/2011
 */

#ifndef __COLIBRI_NET_BUFFER_POOL_H__
#define __COLIBRI_NET_BUFFER_POOL_H__

#include "lib/types.h" /* uint64_t */
#include "lib/mutex.h" /* c2_mutex */
#include "net/net.h"   /* C2_NET_BUFFER*/
#include "lib/tlist.h"

/**
   @defgroup net_buffer_pool Network Buffer Pool

   @brief Network buffer pool allocates and manages a pool of network buffers.
	  Users request a buffer from the pool and after its usage is over
	  gives back to the pool.
   @{
  */

struct c2_net_buffer_pool;

/** Checks the buffer pool. */
bool c2_net_buffer_pool_invariant(const struct c2_net_buffer_pool *pool);

/**
   Initializes a buffer pool.
   @pre pool->nbp_ndom != NULL
   @param threshold Number of buffer below which to notify the user.
   @post c2_net_buffer_pool_invariant(pool)
 */
int c2_net_buffer_pool_init(struct c2_net_buffer_pool *pool,
			    struct c2_net_domain *ndom, uint32_t threshold);

/**
   Populates the buffer pool.
   @pre seg_size > 0 && seg_nr > 0 && buf_nr > 0
   @pre pool->nbp_ndom != NULL
   @pre c2_net_buffer_pool_is_locked(pool)
   @pre (seg_nr * seg_size) <= c2_net_domain_get_max_buffer_size(pool->nbp_ndom)
   @pre seg_size <= c2_net_domain_get_max_buffer_segment_size(pool->nbp_ndom)

   @param buf_nr    Number of buffers in the pool.
   @param seg_nr    Number of segments in each buffer.
   @param seg_size  Size of each segment in a buffer.
 */
int c2_net_buffer_pool_provision(struct c2_net_buffer_pool *pool,
				 uint32_t buf_nr, uint32_t seg_nr,
				 c2_bcount_t seg_size);
/** Finalizes a buffer pool. */
void c2_net_buffer_pool_fini(struct c2_net_buffer_pool *pool);

/** Acquires the lock on buffer pool. */
void c2_net_buffer_pool_lock(struct c2_net_buffer_pool *pool);

/** Check whether buffer pool is locked or not. */
bool c2_net_buffer_pool_is_locked(const struct c2_net_buffer_pool *pool);

/** Releases the lock on buffer pool. */
void c2_net_buffer_pool_unlock(struct c2_net_buffer_pool *pool);

/**
   Gets a buffer from the pool.
   Returns NULL when the pool is empty.
   @pre c2_net_buffer_pool_is_locked(pool)
   @post ergo(result != NULL, result->nb_flags & C2_NET_BUF_REGISTERED)
 */
struct c2_net_buffer *c2_net_buffer_pool_get(struct c2_net_buffer_pool *pool);

/**
   Puts the buffer back to the pool.
   @pre c2_net_buffer_pool_is_locked(pool)
   @pre pool->nbp_ndom == buf->nb_dom
   @pre (buf->nb_flags & C2_NET_BUF_REGISTERED) &&
        !(buf->nb_flags & C2_NET_BUF_IN_USE)
 */
void c2_net_buffer_pool_put(struct c2_net_buffer_pool *pool,
			    struct c2_net_buffer *buf);

/**
   Adds a buffer to the pool to increase the capacity.
   @pre c2_net_buffer_pool_is_locked(pool)
 */
bool c2_net_buffer_pool_grow(struct c2_net_buffer_pool *pool);

/**
   Removes a buffer from the pool to prune it.
   @pre c2_net_buffer_pool_is_locked(pool)
 */
bool c2_net_buffer_pool_prune(struct c2_net_buffer_pool *pool);

/* Buffer pool context. */
struct c2_net_buffer_pool {
	/** Number of free buffers in the pool. */
	uint32_t		nbp_free;
	/** Number of buffer below which low memory condtion occurs. */
	uint32_t		nbp_threshold;
	/** Number of segemnts in each buffer of the pool. */
	uint32_t		nbp_seg_nr;
	/** Number of buffers in the pool. */
	uint32_t		nbp_buf_nr;
	/** Size of buffer segemnt of the pool. */
	c2_bcount_t		nbp_seg_size;
	/** Buffer pool lock. */
	struct c2_mutex		nbp_mutex;
	/** Network domain to register the buffers. */
	struct c2_net_domain   *nbp_ndom;
	/** Head of list of buffers in the pool. */
	struct c2_tl		nbp_head;

};

/** @} end of net_buffer_pool */
#endif
