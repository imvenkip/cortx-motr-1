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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "lib/tlist.h"
#include "lib/memory.h"/* C2_ALLOC_PTR */
#include "lib/thread.h"/* C2_THREAD_INIT */
#include "lib/errno.h" /* ENOMEM */
#include "lib/misc.h"  /* C2_SET0 */
#include "net/net.h"   /* C2_NET_BUFFER*/
#include "net/bulk_sunrpc.h"
#include <unistd.h>
#include <stdio.h>
#include "io_buf_pool.h"

/**
   @addtogroup io_buf_prealloc Buffer pool
   @{
 */

/**
   Initializes a buffer pool.
   Parameters are cap:capacity of the pool
		  buf_size:size of each buffer
		  seg_size:size of each segment in a buffer
 */
int c2_buf_pool_init(struct c2_buf_pool *pool, int cap, int buf_size, int seg_size)
{
	int nr = 0;/* Number of segments in each buffer. */
	int rc = 0;

	struct c2_net_buffer *nb = NULL;

	C2_PRE(pool != NULL);
	pool->bp_threshold = BUF_POOL_THRESHOLD;

	c2_tlist_init(&buf_pool_descr, &pool->bp_head);
	
	do {
		nr = buf_size / seg_size;
		cap -= (nr * seg_size);
	
		C2_ALLOC_PTR(nb);
		if (nb == NULL)
			return -ENOMEM;
		C2_ALLOC_PTR(pool->bp_list);
		if (pool->bp_list == NULL)
			return -ENOMEM;
		pool->bp_list->bl_nb = nb;
		
		rc = c2_bufvec_alloc(&nb->nb_buffer, nr, seg_size);
		if (rc != 0)
			goto clean;

		printf("%lu %lu \n",(unsigned long)nb,(unsigned long)&nb->nb_buffer);

		rc = c2_net_buffer_register(nb, &pool->ndom);
		if (rc != 0)
			goto clean;

		printf("nr %d cap %d \n",nr,cap);

		c2_tlink_init(&buf_pool_descr, pool->bp_list);
		C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, pool->bp_list));
		c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head, pool->bp_list);
		pool->bp_free++;
	} while (cap > 0);

	return rc;
clean :
	c2_buf_pool_fini(pool);
	return rc;
}

/**
   Finalizes a buffer pool.
 */
void c2_buf_pool_fini(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb = NULL;
	int i = 0;
	C2_PRE(pool != NULL);
	c2_tlist_for(&buf_pool_descr, &pool->bp_head, pool->bp_list) {
		nb = pool->bp_list->bl_nb;
		c2_net_buffer_deregister(nb, &pool->ndom);
		c2_bufvec_free(&nb->nb_buffer);
		c2_tlist_del(&buf_pool_descr, pool->bp_list);
		c2_tlink_fini(&buf_pool_descr, pool->bp_list);
		c2_free(nb);
		c2_free(pool->bp_list);
		printf("%d %lu \n",++i,(unsigned long)nb);
		pool->bp_free--;
	}
	c2_tlist_endfor;
	c2_tlist_fini(&buf_pool_descr, &pool->bp_head);
}

/* Acquires the lock on buffer pool. */
void c2_buf_pool_lock(struct c2_buf_pool *pool)
{
	c2_mutex_lock(&pool->bp_lock);
}

/* Releases the lock on buffer pool */
void c2_buf_pool_unlock(struct c2_buf_pool *pool)
{
	c2_mutex_unlock(&pool->bp_lock);
}

/**
   Returns a network buffer the pool.
   Returns NULL when the pool is empty
   @pre pool is locked
 */
struct c2_net_buffer * c2_buf_pool_get(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb = NULL;

	C2_PRE(pool != NULL);
	if(pool->bp_free < pool->bp_threshold) {
		pool->bp_ops->low(pool);
		return NULL;
	}

	pool->bp_list = c2_tlist_head(&buf_pool_descr, &pool->bp_head);
	c2_tlist_del(&buf_pool_descr, pool->bp_list);
	c2_tlink_fini(&buf_pool_descr, pool->bp_list);
	nb = pool->bp_list->bl_nb;
	c2_free(pool->bp_list);
	printf("GET %lu \n",(unsigned long)nb);
	pool->bp_free--;

	return nb;
}

/**
   Returns the buffer back to the pool.
 */
void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf)
{
	C2_PRE(pool != NULL);
	C2_PRE(buf != NULL);
	C2_ALLOC_PTR(pool->bp_list);
	C2_ASSERT(pool->bp_list != NULL);

	pool->bp_list->bl_nb = buf;
	printf("PUT %lu \n",(unsigned long)buf);

	c2_tlink_init(&buf_pool_descr, pool->bp_list);
	C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, pool->bp_list));
	c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head, pool->bp_list);

	pool->bp_free++;
	if(pool->bp_free > pool->bp_threshold)
		pool->bp_ops->notEmpty(pool);
}

/**
   Adds a new buffer to the pool to increase the capacity.
 */
void c2_buf_pool_add(struct c2_buf_pool *pool,struct c2_net_buffer *buf)
{
	int rc = 0;
	C2_PRE(buf != NULL);
	printf("ADD %lu \n",(unsigned long)buf);
	rc = c2_net_buffer_register(buf, &pool->ndom);
	C2_ASSERT(rc == 0);
	c2_buf_pool_put(pool, buf);

}

void NotEmpty(struct c2_buf_pool *bp);
void Low(struct c2_buf_pool *bp);
void buffers_get_put(int rc);
struct c2_buf_pool bp;
struct c2_chan buf_chan;
struct c2_buf_pool_ops b_ops = {
	.notEmpty = NotEmpty,
	.low	  = Low,
};
int main()
{

	int rc;
	struct c2_thread        *client_thread;
	int			 nr_client_threads = 25;
	int i;
	struct c2_net_buffer *nb = NULL;
	struct c2_net_xprt *xprt;
	c2_chan_init(&buf_chan);
	xprt = &c2_net_bulk_sunrpc_xprt;
	c2_net_xprt_init(xprt);
	rc = c2_net_domain_init(&bp.ndom, xprt);
	C2_ASSERT(rc == 0);
	rc = c2_buf_pool_init(&bp, 16384, 512, 256);
	C2_ASSERT(rc == 0);
	bp.bp_ops = &b_ops;
	nb = c2_buf_pool_get(&bp);
	sleep(1);
	c2_buf_pool_put(&bp, nb);
	C2_ALLOC_PTR(nb);
	if (nb == NULL)
		return -ENOMEM;
	rc = c2_bufvec_alloc(&nb->nb_buffer, 10, 1024);
	C2_ASSERT(rc == 0);
	c2_buf_pool_add(&bp, nb);

	C2_ALLOC_ARR(client_thread, nr_client_threads);
	if (client_thread == NULL)
		return -ENOMEM;
	for (i = 0; i < nr_client_threads; i++) {
		C2_SET0(&client_thread[i]);
		rc = C2_THREAD_INIT(&client_thread[i], int,
				     NULL, &buffers_get_put,
					0, "client_%d", i);
		C2_ASSERT(rc == 0);
	}
	for (i = 0; i < nr_client_threads; i++) {
		c2_thread_join(&client_thread[i]);
	}
	c2_buf_pool_fini(&bp);
	c2_net_domain_fini(&bp.ndom);
	c2_net_xprt_fini(xprt);
	c2_chan_fini(&buf_chan);
	return 0;

};

void buffers_get_put(int rc)
{
	struct c2_net_buffer *nb = NULL;
	struct c2_clink buf_link;
	c2_clink_init(&buf_link, NULL);
	c2_clink_add(&buf_chan, &buf_link);
	do {
		c2_buf_pool_lock(&bp);
		nb = c2_buf_pool_get(&bp);
		c2_buf_pool_unlock(&bp);
		if(nb == NULL)
			c2_chan_wait(&buf_link);
	} while(nb == NULL);
	sleep(1);
	c2_buf_pool_lock(&bp);
	if(nb != NULL)
		c2_buf_pool_put(&bp, nb);
	c2_buf_pool_unlock(&bp);
	c2_clink_del(&buf_link);
	c2_clink_fini(&buf_link);
}
void NotEmpty(struct c2_buf_pool *bp)
{
	c2_chan_signal(&buf_chan);
}
void Low(struct c2_buf_pool *bp)
{
	printf("Buffer pool is LOW \n");
}

/** @} end of io_buf_prealloc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

