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

#ifndef __COLIBRI_IOSERVICE_IO_BUF_POOL_H__
#define __COLIBRI_IOSERVICE_IO_BUF_POOL_H__

#include "lib/types.h" /* uint64_t */
#include "lib/mutex.h" /* c2_mutex */
#include "net/net.h"   /* C2_NET_BUFFER*/
#include "lib/tlist.h"

/**
   @defgroup io_buf_prealloc Buffer pool

   @brief Buffer pool allocates and manages a pool of buffers.
	  Users request a buffer from the pool and after its usage is over
	  gives back to the pool.
   @{
  */

struct c2_buf_pool;
struct c2_buf_pool_item;

/**
   Initializes a buffer pool.
   @pre buf_size > 0 && seg_size > 0
   @pre pool->bp_ndom != NULL
   @pre buf_size <= c2_net_domain_get_max_buffer_size(pool->bp_ndom)
   @pre seg_size <= c2_net_domain_get_max_buffer_segment_size(pool->bp_ndom)

   @param buf_nr    Number of buffers in the pool.
   @param seg_nr    Number of segments in each buffer.
   @param seg_size  Size of each segment in a buffer.
   @param threshold Number of buffer below which to notify the user.
 */
int c2_buf_pool_init(struct c2_buf_pool *pool, uint32_t buf_nr, uint32_t seg_nr,
		     c2_bcount_t seg_size, uint32_t threshold);

/**
   Finalizes a buffer pool.
 */
void c2_buf_pool_fini(struct c2_buf_pool *pool);

/** Acquires the lock on buffer pool. */
void c2_buf_pool_lock(struct c2_buf_pool *pool);

/** Check whether buffer pool is locked or not.*/
bool c2_buf_pool_is_locked(const struct c2_buf_pool *pool);

/** Releases the lock on buffer pool. */
void c2_buf_pool_unlock(struct c2_buf_pool *pool);

/**
   Returns a buffer from the pool.
   Returns NULL when the pool is empty.

   @pre c2_buf_pool_is_locked(pool)
 */
struct c2_net_buffer *c2_buf_pool_get(struct c2_buf_pool *pool);

/**
   Returns the buffer back to the pool.
   @pre c2_buf_pool_is_locked(pool)
   @pre (buf->nb_flags & C2_NET_BUF_REGISTERED) &&
        !(buf->nb_flags & C2_NET_BUF_IN_USE)
 */
void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf);

/**
   Adds a buffer to the pool to increase the capacity.
   @pre c2_buf_pool_is_locked(pool)
 */
void c2_buf_pool_add(struct c2_buf_pool *pool);

/** Call backs that buffer pool can trigger on different memory conditions. */
struct c2_buf_pool_ops {
	/** Buffer pool is not empty. */
	void (*bpo_not_empty)(struct c2_buf_pool *);
	/** Buffers in memory are lower than threshold. */
	void (*bpo_below_threshold)(struct c2_buf_pool *);
};

/** It encompasses into the buffer list. */
struct c2_buf_pool_item {
	/** Link for tlist. */
	struct c2_tlink	      bpi_link;
	/** Magic for tlist. */
	uint64_t	      bpi_magic;
	/** List of buffers to be stored in tlist. */
	struct c2_net_buffer  bpi_nb;
};

/* Buffer pool context. */
struct c2_buf_pool {
	/** Number of free buffers in the pool. */
	uint32_t		bp_free;
	/** Number of buffer below which low memory condtion occurs. */
	uint32_t		bp_threshold;
	/** Number of segemnts in each buffer of the pool. */
	uint32_t		seg_nr;
	/** Size of buffer segemnt of the pool. */
	c2_bcount_t		seg_size;
	/** Buffer pool lock. */
	struct c2_mutex		bp_lock;
	/** Call back operations can be triggered buffer pool. */
	const struct c2_buf_pool_ops *bp_ops;
	/** Head of list of buffers in the pool. */
	struct c2_tl		bp_head;
	/** Network domain to register the buffers. */
	struct c2_net_domain   *bp_ndom;
};

enum {
	/* Hex ASCII value of "bpi_link" */
	BUF_POOL_LINK_MAGIC = 0x626c5f6c696e6b,
	/* Hex ASCII value of "bpi_head" */
	BUF_POOL_HEAD_MAGIC = 0x626c5f68656164,
};

/** @} end of io_buf_prealloc */
#endif
