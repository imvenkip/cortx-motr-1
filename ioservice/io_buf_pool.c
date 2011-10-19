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
#include "lib/errno.h" /* ENOMEM */
#include "net/net.h"   /* C2_NET_BUFFER*/
#include "io_buf_pool.h"

/**
   @addtogroup io_buf_prealloc Buffer pool
   @{
 */

int c2_buf_pool_init(struct c2_buf_pool *pool, int cap, int buf_size,
		     int seg_size, int threshold)
{
	int nr = 0; /* Number of segments in each buffer. */
	int rc = 0;

	struct c2_net_buffer *nb = NULL;

	C2_PRE(pool != NULL);
	C2_ASSERT(buf_size <= c2_net_domain_get_max_buffer_size(&pool->ndom));
	C2_ASSERT(seg_size <=
		  c2_net_domain_get_max_buffer_segment_size(&pool->ndom));
	pool->bp_threshold = (threshold == 0) ? BUF_POOL_THRESHOLD : threshold;

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
		rc = c2_net_buffer_register(nb, &pool->ndom);
		if (rc != 0)
			goto clean;
		c2_tlink_init(&buf_pool_descr, pool->bp_list);
		C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, pool->bp_list));
		c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head,
				   pool->bp_list);
		pool->bp_free++;
	} while (cap > 0);

	return rc;
clean :
	c2_buf_pool_fini(pool);
	return rc;
}

void c2_buf_pool_fini(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb = NULL;
	C2_PRE(pool != NULL);
	c2_tlist_for(&buf_pool_descr, &pool->bp_head, pool->bp_list) {
		nb = pool->bp_list->bl_nb;
		c2_net_buffer_deregister(nb, &pool->ndom);
		c2_bufvec_free(&nb->nb_buffer);
		c2_tlist_del(&buf_pool_descr, pool->bp_list);
		c2_tlink_fini(&buf_pool_descr, pool->bp_list);
		c2_free(nb);
		c2_free(pool->bp_list);
		pool->bp_free--;
	}
	c2_tlist_endfor;
	c2_tlist_fini(&buf_pool_descr, &pool->bp_head);
}

void c2_buf_pool_lock(struct c2_buf_pool *pool)
{
	c2_mutex_lock(&pool->bp_lock);
}

void c2_buf_pool_unlock(struct c2_buf_pool *pool)
{
	c2_mutex_unlock(&pool->bp_lock);
}

struct c2_net_buffer * c2_buf_pool_get(struct c2_buf_pool *pool)
{
	struct c2_net_buffer *nb = NULL;

	C2_PRE(pool != NULL);
	if(!pool->bp_free)
		return NULL;

	pool->bp_list = c2_tlist_head(&buf_pool_descr, &pool->bp_head);
	c2_tlist_del(&buf_pool_descr, pool->bp_list);
	c2_tlink_fini(&buf_pool_descr, pool->bp_list);
	nb = pool->bp_list->bl_nb;
	c2_free(pool->bp_list);
	pool->bp_free--;
	if(pool->bp_free < pool->bp_threshold) {
		pool->bp_ops->bpo_below_threshold(pool);
	}
	return nb;
}

void c2_buf_pool_put(struct c2_buf_pool *pool, struct c2_net_buffer *buf)
{
	C2_PRE(pool != NULL);
	C2_PRE(buf != NULL);
	C2_ALLOC_PTR(pool->bp_list);
	C2_ASSERT(pool->bp_list != NULL);
	pool->bp_list->bl_nb = buf;
	c2_tlink_init(&buf_pool_descr, pool->bp_list);
	C2_ASSERT(!c2_tlink_is_in(&buf_pool_descr, pool->bp_list));
	c2_tlist_add_tail(&buf_pool_descr, &pool->bp_head, pool->bp_list);
	pool->bp_free++;
	if(pool->bp_free > 0)
		pool->bp_ops->bpo_not_empty(pool);
}

void c2_buf_pool_add(struct c2_buf_pool *pool,struct c2_net_buffer *buf)
{
	int rc = 0;
	C2_PRE(buf != NULL);
	rc = c2_net_buffer_register(buf, &pool->ndom);
	C2_ASSERT(rc == 0);
	c2_buf_pool_put(pool, buf);
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
