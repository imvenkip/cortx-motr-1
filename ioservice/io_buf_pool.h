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

#ifndef __COLIBRI_IO_BUF_POOL_H__
#define __COLIBRI_IO_BUF_POOL_H__

/**
   @defgroup io_buf_prealloc Buffer pool
   
   @brief Buffer pool allocates and manages a pool of buffers.
	  Users request a buffer from the pool and after its usage is over gives back to the pool.
   @{
  */

enum {
	/* Size of each segment in a network buffer. */
	SEG_SIZE           = 64,
	/* Minimum number of buffers below which low memory condition occurs. */
	BUF_POOL_THRESHOLD = 1
};

/* A pool of buffers and its context.*/
struct c2_buf_pool;
/* A list of buffers. */
struct c2_buf_pool_list;

int c2_buf_pool_init(struct c2_buf_pool *pool, int cap, int buf_size,
		     int seg_size);
void c2_buf_pool_fini(struct c2_buf_pool *pool);
void c2_buf_pool_lock(struct c2_buf_pool *pool);
void c2_buf_pool_unlock(struct c2_buf_pool *pool);
struct c2_net_buffer * c2_buf_pool_get(struct c2_buf_pool *pool);
void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf);
void c2_buf_pool_add(struct c2_buf_pool *pool,struct c2_net_buffer *buf);

/* Call backs that buffer pool can trigger on different memory conditions.*/
struct c2_buf_pool_ops {
	/* Buffer pool is not empty. */
	void (*notEmpty)(struct c2_buf_pool *);
	/* Buffers in memory are lower than threshold. */
	void (*low)(struct c2_buf_pool *);
};

/* Buffer list contains list of buffers. */
struct c2_buf_list {
	/* Link for tlist. */
	struct c2_tlink	      bl_link;
	/* Magic for tlist. */
	uint64_t	      bl_magic;
	/* List of buffers to be stored in tlist. */
	struct c2_net_buffer *bl_nb;
};

/* Buffer pool context. */
struct c2_buf_pool {
	/* Number of free buffers in the pool. */
	uint32_t		bp_free;
	/* Number of buffer below which low memory condtion occurs. */
	uint32_t		bp_threshold;
	/* Buffer pool lock. */
	struct c2_mutex		bp_lock;
	/* The list of buffers */
	struct c2_buf_list     *bp_list;
	/* Call back opeartions can be triggered buffer pool. */
	struct c2_buf_pool_ops *bp_ops;
	/* Head of list of buffers in the pool. */
	struct c2_tl 		bp_head;
	/* Network domain to register the buffers. */
	struct c2_net_domain	ndom;
};

enum {
	/* Hex ASCII value of "bl_link" */
	BUF_POOL_LINK_MAGIC = 0x626c5f6c696e6b,
	/* Hex ASCII value of "bl_head" */
	BUF_POOL_HEAD_MAGIC = 0x626c5f68656164,
};

/* Descriptor for the tlist of buffers. */
static const struct c2_tl_descr buf_pool_descr =
		    C2_TL_DESCR("buf_pool_descr",
		    struct c2_buf_list, bl_link, bl_magic,
		    BUF_POOL_LINK_MAGIC, BUF_POOL_HEAD_MAGIC);

/** @} end of io_buf_prealloc */
#endif
